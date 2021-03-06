/**************************************************************************
  Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
**************************************************************************/


//
// Pthread implementation of Chapel threading interface
//

#ifdef __OPTIMIZE__
// Turn assert() into a no op if the C compiler defines the macro above.
#define NDEBUG
#endif

#include "chpl-comm.h"
#include "chpl-mem.h"
#include "chplcast.h"
#include "chplrt.h"
#include "chpl-tasks.h"
#include "config.h"
#include "error.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
  void* (*fn)(void*);
  void* arg;
} thread_func_t;


//
// Types and variables for our list of threads.
//
typedef struct thread_list* thread_list_p;
struct thread_list {
  pthread_t     thread;
  thread_list_p next;
};

static chpl_bool       exiting = false;         // are we shutting down?

static pthread_mutex_t thread_info_lock;        // mutual exclusion lock

static int64_t         curr_thread_id = 0;

static thread_list_p   thread_list_head = NULL; // head of thread_list
static thread_list_p   thread_list_tail = NULL; // tail of thread_list

static pthread_attr_t  thread_attributes;

static pthread_key_t   thread_id_key;
static pthread_key_t   thread_private_key;

static int32_t         threadMaxThreadsPerLocale  = 0;
static uint32_t        threadNumThreads           = 0;
static pthread_mutex_t threadNumThreadsLock;

static size_t          threadCallStackSize = 0;

static void            (*saved_threadBeginFn)(void*);
static void            (*saved_threadEndFn)(void);

static void*           initial_pthread_func(void*);
static void*           pthread_func(void*);


// Mutexes

void chpl_thread_mutexInit(chpl_thread_mutex_p mutex) {
  // WAW: how to explicitly specify blocking-type?
  if (pthread_mutex_init((pthread_mutex_t*) mutex, NULL))
    chpl_internal_error("pthread_mutex_init() failed");
}

chpl_thread_mutex_p chpl_thread_mutexNew(void) {
  chpl_thread_mutex_p m;
  m = (chpl_thread_mutex_p) chpl_mem_alloc(sizeof(chpl_thread_mutex_t),
                                           CHPL_RT_MD_MUTEX, 0, 0);
  chpl_thread_mutexInit(m);
  return m;
}

void chpl_thread_mutexLock(chpl_thread_mutex_p mutex) {
  if (pthread_mutex_lock((pthread_mutex_t*) mutex))
    chpl_internal_error("pthread_mutex_lock() failed");
}

void chpl_thread_mutexUnlock(chpl_thread_mutex_p mutex) {
  if (pthread_mutex_unlock((pthread_mutex_t*) mutex))
    chpl_internal_error("pthread_mutex_unlock() failed");
}


// Thread management

chpl_thread_id_t chpl_thread_getId(void) {
  void* val = pthread_getspecific(thread_id_key);

  if (val == NULL)
    return chpl_thread_nullThreadId;
  return (chpl_thread_id_t) (intptr_t) val;
}


void chpl_thread_yield(void) {
  int last_cancel_state;

  //
  // Yield the processor.  To support orderly shutdown, we check
  // for cancellation once the thread regains the processor.
  //
  sched_yield();

  (void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &last_cancel_state);
  pthread_testcancel();
  (void) pthread_setcancelstate(last_cancel_state, NULL);
}


void chpl_thread_init(int32_t numThreadsPerLocale,
                      int32_t maxThreadsPerLocale,
                      uint64_t callStackSize,
                      void(*threadBeginFn)(void*),
                      void(*threadEndFn)(void)) {
  //
  // Tuck maxThreadsPerLocale away in a static global for use by other
  // routines.  This threading layer uses a user-specified (non-zero)
  // numThreadsPerLocale as the max.
  //

  if (numThreadsPerLocale != 0) {
    threadMaxThreadsPerLocale = numThreadsPerLocale;
  } else {
    threadMaxThreadsPerLocale = maxThreadsPerLocale;
  }

  //
  // Count the main thread on locale 0 as already existing, since it
  // is (or soon will be) running the main program.
  //
  if (chpl_localeID == 0)
    threadNumThreads = 1;

  //
  // If a value was specified for the call stack size config const, use
  // that (rounded up to a whole number of pages) to set the system and
  // pthread stack limits.
  //
  if (pthread_attr_init(&thread_attributes) != 0)
    chpl_internal_error("pthread_attr_init() failed");

  //
  // If a value was specified for the call stack size config const, use
  // that (rounded up to a whole number of pages) to set the system
  // stack limit.
  //
  if (callStackSize != 0) {
    uint64_t      pagesize = (uint64_t) sysconf(_SC_PAGESIZE);
    struct rlimit rlim;

    callStackSize = (callStackSize + pagesize - 1) & ~(pagesize - 1);

    if (getrlimit(RLIMIT_STACK, &rlim) != 0)
      chpl_internal_error("getrlimit() failed");

    if (rlim.rlim_max != RLIM_INFINITY && callStackSize > rlim.rlim_max) {
      char warning[128];
      sprintf(warning, "callStackSize capped at %lu\n", 
              (unsigned long)rlim.rlim_max);
      chpl_warning(warning, 0, NULL);

      callStackSize = rlim.rlim_max;
    }

    rlim.rlim_cur = callStackSize;

    if (setrlimit(RLIMIT_STACK, &rlim) != 0)
      chpl_internal_error("setrlimit() failed");

    if (pthread_attr_setstacksize(&thread_attributes, callStackSize) != 0)
      chpl_internal_error("pthread_attr_setstacksize() failed");
  }

  if (pthread_attr_getstacksize(&thread_attributes, &threadCallStackSize) != 0)
      chpl_internal_error("pthread_attr_getstacksize() failed");

  saved_threadBeginFn = threadBeginFn;
  saved_threadEndFn   = threadEndFn;

  if (pthread_key_create(&thread_id_key, NULL))
    chpl_internal_error("pthread_key_create(thread_id_key) failed");

  if (pthread_setspecific(thread_id_key, (void*) (intptr_t) --curr_thread_id))
    chpl_internal_error("thread id data key doesn't work");

  if (pthread_key_create(&thread_private_key, NULL))
    chpl_internal_error("pthread_key_create(thread_private_key) failed");

  pthread_mutex_init(&thread_info_lock, NULL);
  pthread_mutex_init(&threadNumThreadsLock, NULL);

  //
  // This is something of a hack, but it makes us a bit more resilient
  // if we're out of memory or near to it at shutdown time.  Launch,
  // cancel, and join with an initial pthread, forcing initialization
  // needed by any of those activities.  (In particular we have found
  // that cancellation needs to dlopen(3) a shared object, which fails
  // if we are out of memory.  Doing it now means that shared object is
  // already available when we need it later.)
  //
  {
    pthread_t initial_pthread;

    if (!pthread_create(&initial_pthread, NULL, initial_pthread_func, NULL)) {
      (void) pthread_cancel(initial_pthread);
      (void) pthread_join(initial_pthread, NULL);
    }
  }
}

//
// The initial pthread just waits to be canceled.  See the comment in
// chpl_thread_init() for the purpose of this.
//
static void* initial_pthread_func(void* ignore) {
  while (1) {
    pthread_testcancel();
    sched_yield();
  }
  return NULL;
}

int chpl_thread_createCommThread(chpl_fn_p fn, void* arg) {
  pthread_t polling_thread;
  return pthread_create(&polling_thread, NULL, (void*(*)(void*))fn, arg);
}

void chpl_thread_exit(void) {
  chpl_bool debug = false;
  thread_list_p tlp;

  pthread_mutex_lock(&thread_info_lock);
  exiting = true;

  // shut down all threads
  for (tlp = thread_list_head; tlp != NULL; tlp = tlp->next) {
    if (pthread_cancel(tlp->thread) != 0)
      chpl_internal_error("thread cancel failed");
  }

  pthread_mutex_unlock(&thread_info_lock);

  while (thread_list_head != NULL) {
    if (pthread_join(thread_list_head->thread, NULL) != 0)
      chpl_internal_error("thread join failed");
    tlp = thread_list_head;
    thread_list_head = thread_list_head->next;
    chpl_mem_free(tlp, 0, 0);
  }

  if (pthread_key_delete(thread_id_key) != 0)
    chpl_internal_error("pthread_key_delete(thread_id_key) failed");

  if (pthread_attr_destroy(&thread_attributes) != 0)
    chpl_internal_error("pthread_attr_destroy() failed");

  if (debug)
    fprintf(stderr, "A total of %u threads were created\n", threadNumThreads);
}

chpl_bool chpl_thread_canCreate(void) {
  return (threadMaxThreadsPerLocale == 0 ||
          threadNumThreads < (uint32_t) threadMaxThreadsPerLocale);
}

int chpl_thread_create(void* arg)
{
  //
  // An implementation note:
  //
  // It's important to keep the thread counter as accurate as possible,
  // because it's used to throttle thread creation so that we don't go
  // over the user's specified limit.  We could count the new thread
  // when it starts executing, in pthread_func().  But if the kernel
  // executed parent threads in preference to children, the resulting
  // delay in updating the counter could cause us to create many more
  // threads than the limit.  Or we could count them after creating
  // them, here.  But if grabbing the mutex that protects the counter
  // stalled the parent and led the kernel to schedule other threads,
  // updates to the counter could again be delayed and too many threads
  // created.  The solution adopted is to update the counter in the
  // parent before creating the new thread, and then decrement it if
  // thread creation fails.  The idea is that if the only thing that
  // separates the counter update from the thread creation is a mutex
  // unlock which won't cause the parent to be rescheduled, we maximize
  // the likelihood that everyone will see accurate counter values.
  //

  pthread_t pthread;

  pthread_mutex_lock(&threadNumThreadsLock);
  threadNumThreads++;
  pthread_mutex_unlock(&threadNumThreadsLock);

  if (pthread_create(&pthread, &thread_attributes, pthread_func, arg)) {
    pthread_mutex_lock(&threadNumThreadsLock);
    threadNumThreads--;
    pthread_mutex_unlock(&threadNumThreadsLock);

    return -1;
  }

  return 0;
}

static void* pthread_func(void* arg) {
  chpl_thread_id_t my_thread_id;
  thread_list_p          tlp;

  // disable cancellation immediately
  // enable only while waiting for new work
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); 

  // add us to the list of threads
  tlp = (thread_list_p) chpl_mem_alloc(sizeof(struct thread_list),
                                       CHPL_RT_MD_THREAD_LIST_DESCRIPTOR, 0, 0);

  tlp->thread = pthread_self();
  tlp->next   = NULL;

  pthread_mutex_lock(&thread_info_lock);

  if (exiting) {
    pthread_mutex_unlock(&thread_info_lock);
    chpl_mem_free(tlp, 0, 0);
    return NULL;
  }

  my_thread_id = --curr_thread_id;

  if (thread_list_head == NULL)
    thread_list_head = tlp;
  else
    thread_list_tail->next = tlp;
  thread_list_tail = tlp;

  pthread_mutex_unlock(&thread_info_lock);

  if (pthread_setspecific(thread_id_key, (void*) (intptr_t) my_thread_id))
    chpl_internal_error("thread id data key doesn't work");

  if (saved_threadEndFn == NULL)
    (*saved_threadBeginFn)(arg);
  else {
    pthread_cleanup_push((void (*)(void*)) saved_threadEndFn, NULL);
    (*saved_threadBeginFn)(arg);
    pthread_cleanup_pop(0);
  }

  return NULL;
}

void chpl_thread_destroy(void) {
  int last_cancel_state;

  //
  // Creating threads is expensive, so we don't destroy them once we
  // have them.  Instead we just yield the processor.  Eventually, the
  // thread will return to its caller, which is allowed behavior under
  // the threadlayer API.
  //
  // To support orderly shutdown, we check for cancellation once the
  // thread regains the processor.
  //
  sched_yield();

  (void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &last_cancel_state);
  pthread_testcancel();
  (void) pthread_setcancelstate(last_cancel_state, NULL);
}

void* chpl_thread_getPrivateData(void) {
  return pthread_getspecific(thread_private_key);
}

void chpl_thread_setPrivateData(void* p) {
  if (pthread_setspecific(thread_private_key, p))
    chpl_internal_error("thread private data key doesn't work");
}

uint32_t chpl_thread_getMaxThreads(void) {
  return threadMaxThreadsPerLocale;
}

uint32_t chpl_thread_getNumThreads(void) {
  return threadNumThreads;
}

uint64_t chpl_thread_getCallStackSize(void) {
    return (uint64_t) threadCallStackSize;
}

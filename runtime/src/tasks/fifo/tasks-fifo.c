/**************************************************************************
  Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
**************************************************************************/


//
// FIFO implementation of Chapel tasking interface
//

#ifdef __OPTIMIZE__
// Turn assert() into a no op if the C compiler defines the macro above.
#define NDEBUG
#endif

#include "chpl_rt_utils_static.h"
#include "chplcgfns.h"
#include "chpl-comm.h"
#include "chplexit.h"
#include "chpl-mem.h"
#include "chplrt.h"
#include "chpl-tasks.h"
#include "error.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>


//
// task pool: linked list of tasks
//
typedef struct task_pool_struct* task_pool_p;

typedef struct task_pool_struct {
  chpl_taskID_t    id;           // task identifier
  chpl_fn_p        fun;          // function to call for task
  void*            arg;          // argument to the function
  chpl_bool        serial_state; // whether new tasks can be created while executing fun
  chpl_bool        begun;        // whether execution of this task has begun
  chpl_task_list_p ltask;        // points to the task list entry, if there is one
  chpl_string      filename;
  int              lineno;
  task_pool_p      next;
  task_pool_p      prev;
} task_pool_t;


// This struct is intended for use in a circular linked list where the pointer
// to the list actually points to the tail of the list, i.e., the last entry
// inserted into the list, making it easier to append items to the end of the list.
// Since it is part of a circular list, the last entry will, of course,
// point to the first entry in the list.
struct chpl_task_list {
  chpl_fn_p fun;
  void* arg;
  volatile task_pool_p ptask; // when null, execution of the associated task has begun
  chpl_string filename;
  int lineno;
  chpl_task_list_p next;
};


typedef struct lockReport {
  const char*        filename;
  int                lineno;
  uint64_t           prev_progress_cnt;
  chpl_bool          maybeLocked;
  struct lockReport* next;
} lockReport_t;


// This is the data that is private to each thread.
typedef struct {
  task_pool_p   ptask;
  lockReport_t* lockRprt;
} thread_private_data_t;


static chpl_bool        initialized = false;

static chpl_thread_mutex_t threading_lock;     // critical section lock
static chpl_thread_mutex_t extra_task_lock;    // critical section lock
static chpl_thread_mutex_t task_id_lock;       // critical section lock
static chpl_thread_mutex_t task_list_lock;     // critical section lock
static volatile task_pool_p
                           task_pool_head;     // head of task pool
static volatile task_pool_p
                           task_pool_tail;     // tail of task pool

static int                 queued_task_cnt;    // number of tasks in task pool
static int                 running_task_cnt;   // number of running tasks
static int64_t             extra_task_cnt;     // number of tasks being run by
                                               //   threads occupied already
static int                 waking_thread_cnt;  // number of threads created but
                                               //   not yet running
static int                 blocked_thread_cnt; // number of threads that
                                               //   cannot make progress
static int                 idle_thread_cnt;    // number of threads looking
                                               //   for work
static uint64_t            progress_cnt;       // number of unblock operations,
                                               //   as a proxy for progress

static chpl_thread_mutex_t block_report_lock;  // critical section lock
static chpl_thread_mutex_t taskTable_lock;     // critical section lock

static lockReport_t* lockReportHead = NULL;
static lockReport_t* lockReportTail = NULL;

static chpl_string idleTaskName = "|idle|";

static chpl_fn_p comm_task_fn;

static void                    comm_task_wrapper(void*);
static chpl_taskID_t           get_next_task_id(void);
static thread_private_data_t*  get_thread_private_data(void);
static task_pool_p             get_current_ptask(void);
static void                    set_current_ptask(task_pool_p);
static void                    report_locked_threads(void);
static void                    report_all_tasks(void);
static void                    SIGINT_handler(int sig);
static void                    initializeLockReportForThread(void);
static chpl_bool               set_block_loc(int, chpl_string);
static void                    unset_block_loc(void);
static void                    check_for_deadlock(void);
static void                    thread_begin(void*);
static void                    thread_end(void);
static void                    launch_next_task_in_new_thread(void);
static void                    schedule_next_task(int);
static task_pool_p             add_to_task_pool(chpl_fn_p,
                                                void*,
                                                chpl_bool,
                                                chpl_task_list_p);


// Sync variables

static void sync_wait_and_lock(chpl_sync_aux_t *s,
                               chpl_bool want_full,
                               int32_t lineno, chpl_string filename) {
  chpl_thread_mutexLock(&s->lock);

  while (s->is_full != want_full) {
    chpl_thread_mutexUnlock(&s->lock);

    while (s->is_full != want_full) {
      if (set_block_loc(lineno, filename)) {
        // all other tasks appear to be blocked
        struct timeval deadline, now;
        gettimeofday(&deadline, NULL);
        deadline.tv_sec += 1;
        do {
          chpl_thread_yield();
          if (s->is_full != want_full)
            gettimeofday(&now, NULL);
        } while (s->is_full != want_full
                 && (now.tv_sec < deadline.tv_sec
                     || (now.tv_sec == deadline.tv_sec
                         && now.tv_usec < deadline.tv_usec)));
        if (s->is_full != want_full)
          check_for_deadlock();
      }
      else {
        do {
          chpl_thread_yield();
        } while (s->is_full != want_full);
      }

      unset_block_loc();
    }

    chpl_thread_mutexLock(&s->lock);
  }

  if (blockreport)
    progress_cnt++;
}

void chpl_sync_lock(chpl_sync_aux_t *s) {
  chpl_thread_mutexLock(&s->lock);
}

void chpl_sync_unlock(chpl_sync_aux_t *s) {
  chpl_thread_mutexUnlock(&s->lock);
}

void chpl_sync_waitFullAndLock(chpl_sync_aux_t *s,
                                  int32_t lineno, chpl_string filename) {
  sync_wait_and_lock(s, true, lineno, filename);
}

void chpl_sync_waitEmptyAndLock(chpl_sync_aux_t *s,
                                   int32_t lineno, chpl_string filename) {
  sync_wait_and_lock(s, false, lineno, filename);
}

void chpl_sync_markAndSignalFull(chpl_sync_aux_t *s) {
  s->is_full = true;
  chpl_sync_unlock(s);
}

void chpl_sync_markAndSignalEmpty(chpl_sync_aux_t *s) {
  s->is_full = false;
  chpl_sync_unlock(s);
}

chpl_bool chpl_sync_isFull(void *val_ptr,
                            chpl_sync_aux_t *s,
                            chpl_bool simple_sync_var) {
  return s->is_full;
}

void chpl_sync_initAux(chpl_sync_aux_t *s) {
  s->is_full = false;
  chpl_thread_mutexInit(&s->lock);
}

void chpl_sync_destroyAux(chpl_sync_aux_t *s) { }


// Tasks

void chpl_task_init(int32_t numThreadsPerLocale, int32_t maxThreadsPerLocale, 
                    int numCommTasks, uint64_t callStackSize) {
  chpl_thread_mutexInit(&threading_lock);
  chpl_thread_mutexInit(&extra_task_lock);
  chpl_thread_mutexInit(&task_id_lock);
  chpl_thread_mutexInit(&task_list_lock);
  queued_task_cnt = 0;
  running_task_cnt = 1;                     // only main task running
  waking_thread_cnt = 0;
  blocked_thread_cnt = 0;
  idle_thread_cnt = 0;
  extra_task_cnt = 0;
  task_pool_head = task_pool_tail = NULL;

  chpl_thread_init(numThreadsPerLocale, maxThreadsPerLocale, callStackSize,
                   thread_begin, thread_end);

  if (taskreport) {
    chpl_thread_mutexInit(&taskTable_lock);
  }

  if (blockreport) {
    progress_cnt = 0;
    chpl_thread_mutexInit(&block_report_lock);
  }

  //
  // Set main thread private data, so that things that require access
  // to it, like chpl_task_getID() and chpl_task_setSerial(), can be
  // called early (notably during standard module initialization).
  //
  // This needs to be done after the threading layer initialization,
  // because it's based on thread layer capabilities, but before we
  // install the signal handlers, because when those are invoked they
  // may use the thread private data.
  //
  {
    thread_private_data_t* tp;

    tp = (thread_private_data_t*) chpl_mem_alloc(sizeof(thread_private_data_t),
                                                 CHPL_RT_MD_THREAD_PRIVATE_DATA,
                                                 0, 0);

    tp->ptask = (task_pool_p) chpl_mem_alloc(sizeof(task_pool_t),
                                             CHPL_RT_MD_TASK_POOL_DESCRIPTOR,
                                             0, 0);
    tp->ptask->id           = get_next_task_id();
    tp->ptask->fun          = NULL;
    tp->ptask->arg          = NULL;
    tp->ptask->serial_state = true;     // Set to false in chpl_task_callMain().
    tp->ptask->ltask        = NULL;
    tp->ptask->begun        = true;
    tp->ptask->filename     = "main program";
    tp->ptask->lineno       = 0;
    tp->ptask->next         = NULL;

    tp->lockRprt = NULL;

    chpl_thread_setPrivateData(tp);
  }

  if (blockreport || taskreport) {
    signal(SIGINT, SIGINT_handler);
  }

  initialized = true;
}


void chpl_task_exit(void) {
  if (!initialized)
    return;

  chpl_thread_exit();
}


void chpl_task_callMain(void (*chpl_main)(void)) {
  chpl_main();
}


void chpl_task_stdModulesInitialized(void) {
  //
  // Register this main task in the task table.  We couldn't do this
  // at creation time because the standard modules had not yet been
  // initialized and therefore the task table didn't exist yet.
  //
  if (taskreport) {
    thread_private_data_t* tp = chpl_thread_getPrivateData();

    chpldev_taskTable_add(tp->ptask->id,
                          tp->ptask->lineno, tp->ptask->filename,
                          (uint64_t) (intptr_t) tp->ptask);
    chpldev_taskTable_set_active(tp->ptask->id);
  }

  if (blockreport) {
    initializeLockReportForThread();
  }
}


int chpl_task_createCommTask(chpl_fn_p fn, void* arg) {
  comm_task_fn = fn;
  return chpl_thread_createCommThread(comm_task_wrapper, arg);
}


static void comm_task_wrapper(void* arg) {
  thread_private_data_t* tp;

  tp = (thread_private_data_t*) chpl_mem_alloc(sizeof(thread_private_data_t),
                                               CHPL_RT_MD_THREAD_PRIVATE_DATA,
                                               0, 0);

  tp->ptask = (task_pool_p) chpl_mem_alloc(sizeof(task_pool_t),
                                           CHPL_RT_MD_TASK_POOL_DESCRIPTOR,
                                           0, 0);
  tp->ptask->id           = get_next_task_id();
  tp->ptask->fun          = comm_task_fn;
  tp->ptask->arg          = arg;
  tp->ptask->serial_state = true;
  tp->ptask->ltask        = NULL;
  tp->ptask->begun        = true;
  tp->ptask->filename     = "communication task";
  tp->ptask->lineno       = 0;
  tp->ptask->next         = NULL;

  tp->lockRprt = NULL;

  chpl_thread_setPrivateData(tp);

  (*comm_task_fn)(arg);
}


void chpl_task_addToTaskList(chpl_fn_int_t fid, void* arg,
                           chpl_task_list_p *task_list,
                           int32_t task_list_locale,
                           chpl_bool call_chpl_begin,
                           int lineno,
                           chpl_string filename) {
  if (task_list_locale == chpl_localeID) {
    chpl_task_list_p ltask;

    ltask = (chpl_task_list_p) chpl_mem_alloc(sizeof(struct chpl_task_list),
                                              CHPL_RT_MD_TASK_LIST_DESCRIPTOR,
                                              0, 0);
    ltask->filename = filename;
    ltask->lineno   = lineno;
    ltask->fun      = chpl_ftable[fid];
    ltask->arg      = arg;
    ltask->ptask    = NULL;
    if (call_chpl_begin) {
      chpl_fn_p fp = chpl_ftable[fid];
      chpl_task_begin(fp, arg, false, false, ltask);
    }

    // begin critical section - not needed for cobegin or coforall statements
    if (call_chpl_begin)
      chpl_thread_mutexLock(&task_list_lock);

    if (*task_list) {
      ltask->next = (*task_list)->next;
      (*task_list)->next = ltask;
    }
    else
      ltask->next = ltask;
    *task_list = ltask;

    // end critical section - not needed for cobegin or coforall statements
    if (call_chpl_begin)
      chpl_thread_mutexUnlock(&task_list_lock);
  }
  else {
    // call_chpl_begin should be true here because if task_list_locale !=
    // chpl_localeID, then this function could not have been called from
    // the context of a cobegin or coforall statement, which are the only
    // contexts in which chpl_task_begin() should not be called.
    chpl_fn_p fp = chpl_ftable[fid];
    assert(call_chpl_begin);
    chpl_task_begin(fp, arg, false, false, NULL);
  }
}


void chpl_task_processTaskList(chpl_task_list_p task_list) {
  // task_list points to the last entry on the list; task_list->next is
  // actually the first element on the list.
  chpl_task_list_p ltask = task_list, next_task;
  task_pool_p curr_ptask;
  task_pool_t nested_task;

  // This function is not expected to be called if a cobegin contains fewer
  // than two statements; a coforall, however, may generate just one task,
  // or even none at all.
  if (ltask == NULL)
    return;
  assert(ltask->next);
  next_task = ltask->next;  // next_task now points to the head of the list

  curr_ptask = get_current_ptask();

  if (curr_ptask->serial_state) {
    do {
      ltask = next_task;
      (*ltask->fun)(ltask->arg);
      next_task = ltask->next;
    } while (ltask != task_list);
  } else {
    int task_cnt = 0;
    chpl_task_list_p first_task = next_task;
    next_task = next_task->next;

    if (first_task != task_list) {
      // there are at least two tasks in task_list

      // begin critical section
      chpl_thread_mutexLock(&threading_lock);

      do {
        ltask = next_task;
        ltask->ptask = add_to_task_pool(ltask->fun, ltask->arg,
                                        curr_ptask->serial_state, ltask);
        assert(ltask->ptask == NULL
               || ltask->ptask->ltask == ltask);
        next_task = ltask->next;
        task_cnt++;
      } while (ltask != task_list);

      schedule_next_task(task_cnt);

      // end critical section
      chpl_thread_mutexUnlock(&threading_lock);
    }

    // Execute the first task on the list, since it has to run to completion
    // before continuing beyond the cobegin or coforall it's in.
    nested_task.id           = get_next_task_id();
    nested_task.fun          = first_task->fun;
    nested_task.arg          = first_task->arg;
    nested_task.serial_state = false;
    nested_task.ltask        = first_task;
    nested_task.begun        = true;
    nested_task.filename     = first_task->filename;
    nested_task.lineno       = first_task->lineno;
    set_current_ptask(&nested_task);

    if (taskreport) {
      chpl_thread_mutexLock(&taskTable_lock);
      chpldev_taskTable_add(nested_task.id,
                            nested_task.lineno, nested_task.filename,
                            (uint64_t) (intptr_t) &nested_task);
      chpldev_taskTable_set_suspended(curr_ptask->id);
      chpldev_taskTable_set_active(nested_task.id);
      chpl_thread_mutexUnlock(&taskTable_lock);
    }

    // begin critical section
    chpl_thread_mutexLock(&extra_task_lock);

    extra_task_cnt++;

    // end critical section
    chpl_thread_mutexUnlock(&extra_task_lock);

    if (blockreport)
      initializeLockReportForThread();

    (*first_task->fun)(first_task->arg);

    // begin critical section
    chpl_thread_mutexLock(&extra_task_lock);

    extra_task_cnt--;

    // end critical section
    chpl_thread_mutexUnlock(&extra_task_lock);

    if (taskreport) {
      chpl_thread_mutexLock(&taskTable_lock);
      chpldev_taskTable_set_active(curr_ptask->id);
      chpldev_taskTable_remove(nested_task.id);
      chpl_thread_mutexUnlock(&taskTable_lock);
    }

    set_current_ptask(curr_ptask);
  }
}


void chpl_task_executeTasksInList(chpl_task_list_p task_list) {
  // task_list points to the last entry on the list; task_list->next is
  // actually the first element on the list.
  chpl_task_list_p ltask = task_list, next_task;
  // This function is not expected to be called if a cobegin contains fewer
  // than two statements; a coforall, however, may generate just one task,
  // or even none at all.
  if (ltask == NULL)
    return;
  assert(ltask->next);
  next_task = ltask->next;  // next_task now points to the head of the list

  // If the serial state is true, the tasks in task_list have already been
  // executed.
  if (!chpl_task_getSerial()) do {
    ltask = next_task;
    next_task = ltask->next;

    // don't lock unless it looks like we will find a task to execute
    // if we do so
    if (ltask->ptask) {
      task_pool_p  curr_ptask;
      task_pool_p  nested_ptask = NULL;
      chpl_fn_p    task_to_run_fun = NULL;
      void*        task_to_run_arg = NULL;

      // begin critical section
      chpl_thread_mutexLock(&threading_lock);

      if (ltask->ptask) {
        assert(!ltask->ptask->begun);
        task_to_run_fun = ltask->ptask->fun;
        task_to_run_arg = ltask->ptask->arg;
        ltask->ptask->begun = true;
        ltask->ptask->ltask = NULL;
        // there is no longer any need to access the corresponding task
        // pool entry so avoid any potential of accessing a node that
        // will eventually be freed
        nested_ptask = ltask->ptask;
        ltask->ptask = NULL;
        if (waking_thread_cnt > 0)
          waking_thread_cnt--;
        assert(queued_task_cnt > 0);
        queued_task_cnt--;
        if (nested_ptask->prev == NULL) {
          if ((task_pool_head = nested_ptask->next) == NULL)
            task_pool_tail = NULL;
          else
            task_pool_head->prev = NULL;
        }
        else {
          nested_ptask->prev->next = nested_ptask->next;
          if (nested_ptask->next == NULL)
            task_pool_tail = nested_ptask->prev;
          else
            nested_ptask->next->prev = nested_ptask->prev;
        }
      }

      // end critical section
      chpl_thread_mutexUnlock(&threading_lock);

      if (task_to_run_fun) {
        curr_ptask = get_current_ptask();
        set_current_ptask(nested_ptask);

        // begin critical section
        chpl_thread_mutexLock(&extra_task_lock);

        extra_task_cnt++;

        // end critical section
        chpl_thread_mutexUnlock(&extra_task_lock);

        if (taskreport) {
          chpl_thread_mutexLock(&taskTable_lock);
          chpldev_taskTable_set_suspended(curr_ptask->id);
          chpldev_taskTable_set_active(nested_ptask->id);
          chpl_thread_mutexUnlock(&taskTable_lock);
        }

        if (blockreport)
          initializeLockReportForThread();

        (*task_to_run_fun)(task_to_run_arg);

        if (taskreport) {
          chpl_thread_mutexLock(&taskTable_lock);
          chpldev_taskTable_set_active(curr_ptask->id);
          chpldev_taskTable_remove(nested_ptask->id);
          chpl_thread_mutexUnlock(&taskTable_lock);
        }

        // begin critical section
        chpl_thread_mutexLock(&extra_task_lock);

        extra_task_cnt--;

        // end critical section
        chpl_thread_mutexUnlock(&extra_task_lock);

        set_current_ptask(curr_ptask);
        chpl_mem_free(nested_ptask, 0, 0);
      }
    }

  } while (ltask != task_list);
}


void chpl_task_freeTaskList(chpl_task_list_p task_list) {
  // task_list points to the last entry on the list; task_list->next is
  // actually the first element on the list.
  chpl_task_list_p ltask = task_list, next_task;
  // This function is not expected to be called if a cobegin contains fewer
  // than two statements; a coforall, however, may generate just one task,
  // or even none at all.
  if (ltask == NULL)
    return;
  assert(ltask->next);
  next_task = ltask->next;  // next_task now points to the head of the list

  do {
    ltask = next_task;
    next_task = ltask->next;
    chpl_mem_free(ltask, 0, 0);
  } while (ltask != task_list);
}


//
// interface function with begin-statement
//
void chpl_task_begin(chpl_fn_p fp, void* a,
                chpl_bool ignore_serial,  // always add task to pool
                chpl_bool serial_state,
                chpl_task_list_p ltask) {
  if (!ignore_serial && chpl_task_getSerial()) {
    (*fp)(a);
  } else {
    task_pool_p ptask = NULL;

    // begin critical section
    chpl_thread_mutexLock(&threading_lock);

    ptask = add_to_task_pool(fp, a, serial_state, ltask);
    // this task may begin executing before returning from this function,
    // so the task list node needs to be updated before there is any
    // possibility of launching this task
    if (ltask)
      ltask->ptask = ptask;

    schedule_next_task(1);

    assert(ptask->ltask == NULL
           || (ptask->ltask == ltask
               && ltask->ptask == ptask));

    // end critical section
    chpl_thread_mutexUnlock(&threading_lock);
  }
}


chpl_taskID_t chpl_task_getId(void) {
  return get_current_ptask()->id;
}


void chpl_task_yield(void) {
  chpl_thread_yield();
}


void chpl_task_sleep(int secs) {
  sleep(secs);
}


chpl_bool chpl_task_getSerial(void) {
  return get_thread_private_data()->ptask->serial_state;
}

void chpl_task_setSerial(chpl_bool state) {
  get_thread_private_data()->ptask->serial_state = state;
}

uint64_t chpl_task_getCallStackSize(void) {
  return chpl_thread_getCallStackSize();
}

uint32_t chpl_task_getNumQueuedTasks(void) { return queued_task_cnt; }

uint32_t chpl_task_getNumRunningTasks(void) {
  int numRunningTasks;

  // begin critical section
  chpl_thread_mutexLock(&threading_lock);
  chpl_thread_mutexLock(&extra_task_lock);

  numRunningTasks = running_task_cnt + extra_task_cnt;

  // end critical section
  chpl_thread_mutexUnlock(&extra_task_lock);
  chpl_thread_mutexUnlock(&threading_lock);

  return numRunningTasks;
}

int32_t  chpl_task_getNumBlockedTasks(void) {
  if (blockreport) {
    int numBlockedTasks;

    // begin critical section
    chpl_thread_mutexLock(&threading_lock);
    chpl_thread_mutexLock(&block_report_lock);

    numBlockedTasks = blocked_thread_cnt - idle_thread_cnt;

    // end critical section
    chpl_thread_mutexUnlock(&block_report_lock);
    chpl_thread_mutexUnlock(&threading_lock);

    assert(numBlockedTasks >= 0);
    return numBlockedTasks;
  }
  else
    return -1;
}


// Internal utility functions for task management

//
// Get a new task ID.
//
static chpl_taskID_t get_next_task_id(void) {
  static chpl_taskID_t       id = chpl_nullTaskID + 1;

  chpl_taskID_t              next_id;

  chpl_thread_mutexLock(&task_id_lock);
  next_id = id++;
  chpl_thread_mutexUnlock(&task_id_lock);

  return next_id;
}


//
// Get the the thread private data pointer for my thread.
//
static thread_private_data_t* get_thread_private_data(void) {
  thread_private_data_t* tp;

  tp = (thread_private_data_t*) chpl_thread_getPrivateData();
  if (tp == NULL)
    chpl_internal_error("no thread private data");

  return tp;
}


//
// Get the descriptor for the task now running on my thread.
//
static task_pool_p get_current_ptask(void) {
  return get_thread_private_data()->ptask;
}


//
// Set the descriptor for the task now running on my thread.
//
static void set_current_ptask(task_pool_p ptask) {
  get_thread_private_data()->ptask = ptask;
}


//
// Walk over the linked list of thread states and print the ones that
// are blocked/waiting.  This is used by both the deadlock reporting
// and the ^C signal handler.
//
static void report_locked_threads(void) {
  lockReport_t* rep;

  fflush(stdout);

  rep = lockReportHead;
  while (rep != NULL) {
    if (rep->maybeLocked) {
      if (rep->lineno > 0 && rep->filename)
        fprintf(stderr, "Waiting at: %s:%d\n", rep->filename, rep->lineno);
      else if (rep->lineno == 0 && !strcmp(rep->filename, idleTaskName))
        fprintf(stderr, "Waiting for more work\n");
    }
    rep = rep->next;
  }

  fflush(stdout);
}


//
// This signal handler prints an overall task report, containing
// pending tasks and those that are running.
//
static void report_all_tasks(void) {
    task_pool_p pendingTask = task_pool_head;

    printf("Task report\n");
    printf("--------------------------------\n");

    // print out pending tasks
    printf("Pending tasks:\n");
    while(pendingTask != NULL) {
        if(! pendingTask->begun) {
            printf("- %s:%d\n", pendingTask->filename,
                   (int)pendingTask->lineno);
        }
        pendingTask = pendingTask->next;
    }
    printf("\n");

    // print out running tasks
    printf("Known tasks:\n");
    chpldev_taskTable_print();
}


//
// This is a signal handler that does thread and task reporting.
//
static void SIGINT_handler(int sig) {
  signal(sig, SIG_IGN);

  if (blockreport)
    report_locked_threads();

  if (taskreport)
    report_all_tasks();

  chpl_exit_any(1);
}


//
// This function should be called exactly once per thread (not task!),
// including the main thread. It should be called before the first task
// this thread was created to do is started.
//
// Our handling of lock report list entries could be improved.  We
// allocate one each time this function is called, and this is called
// just before each task wrapper is called.  We never remove these
// from the list or deallocate them.  If we do traverse the list while
// reporting a deadlock, we just skip the leaked ones, because they
// don't say "blocked".
//
static void initializeLockReportForThread(void) {
  lockReport_t* newLockReport;

  newLockReport = (lockReport_t*) chpl_mem_alloc(sizeof(lockReport_t),
                                                 CHPL_RT_MD_LOCK_REPORT_DATA,
                                                 0, 0);
  newLockReport->maybeLocked = false;
  newLockReport->next = NULL;

  get_thread_private_data()->lockRprt = newLockReport;

  // Begin critical section
  chpl_thread_mutexLock(&block_report_lock);
  if (lockReportHead) {
    lockReportTail->next = newLockReport;
    lockReportTail = newLockReport;
  } else {
    lockReportHead = newLockReport;
    lockReportTail = newLockReport;
  }
  // End critical section
  chpl_thread_mutexUnlock(&block_report_lock);
}



// Deadlock detection

//
// Inform task management that the thread (task) is about to suspend
// waiting for a sync or single variable to change state or the task
// pool to become nonempty.  The return value is true if the program
// may be deadlocked, indicating that the thread should use a timeout
// deadline on its suspension if possible, and false otherwise.
//
static chpl_bool set_block_loc(int lineno, chpl_string filename) {
  thread_private_data_t* tp;
  chpl_bool isLastUnblockedThread;

  if (!blockreport)
    return false;

  isLastUnblockedThread = false;

  tp = get_thread_private_data();
  tp->lockRprt->filename = filename;
  tp->lockRprt->lineno = lineno;
  tp->lockRprt->prev_progress_cnt = progress_cnt;
  tp->lockRprt->maybeLocked = true;

  // Begin critical section
  chpl_thread_mutexLock(&block_report_lock);

  blocked_thread_cnt++;
  if (blocked_thread_cnt >= chpl_thread_getNumThreads()) {
    isLastUnblockedThread = true;
  }

  // End critical section
  chpl_thread_mutexUnlock(&block_report_lock);

  return isLastUnblockedThread;
}


//
// Inform task management that the thread (task) is no longer suspended.
//
static void unset_block_loc(void) {
  if (!blockreport)
    return;

  get_thread_private_data()->lockRprt->maybeLocked = false;

  // Begin critical section
  chpl_thread_mutexLock(&block_report_lock);

  blocked_thread_cnt--;

  // End critical section
  chpl_thread_mutexUnlock(&block_report_lock);
}


//
// Check for and report deadlock, when a suspension deadline passes.
//
static void check_for_deadlock(void) {
  // Blockreport should be true here, because this can't be called
  // unless set_block_loc() returns true, and it can't do that unless
  // blockreport is true.  So this is just a check for ongoing
  // internal consistency.
  assert(blockreport);

  if (get_thread_private_data()->lockRprt->prev_progress_cnt < progress_cnt)
    return;

  fflush(stdout);
  fprintf(stderr, "Program is deadlocked!\n");

  report_locked_threads();

  if (taskreport)
    report_all_tasks();

  chpl_exit_any(1);
}


//
// When we create a thread it runs this wrapper function, which just
// executes tasks out of the pool as they become available.
//
static void
thread_begin(void* ptask_void) {
  task_pool_p ptask = (task_pool_p) ptask_void;
  thread_private_data_t *tp;

  tp = (thread_private_data_t*) chpl_mem_alloc(sizeof(thread_private_data_t),
                                               CHPL_RT_MD_THREAD_PRIVATE_DATA,
                                               0, 0);
  tp->ptask    = ptask;
  tp->lockRprt = NULL;
  chpl_thread_setPrivateData(tp);

  if (blockreport)
    initializeLockReportForThread();

  while (true) {
    if (taskreport) {
      chpl_thread_mutexLock(&taskTable_lock);
      chpldev_taskTable_set_active(ptask->id);
      chpl_thread_mutexUnlock(&taskTable_lock);
    }

    (*ptask->fun)(ptask->arg);

    if (taskreport) {
      chpl_thread_mutexLock(&taskTable_lock);
      chpldev_taskTable_remove(ptask->id);
      chpl_thread_mutexUnlock(&taskTable_lock);
    }

    // begin critical section
    chpl_thread_mutexLock(&threading_lock);

    //
    // We have to wait to free the ptask until we hold the lock, in
    // order to make sure launch_next_task_in_new_thread() is done
    // manipulating the ptask before anyone else could re-allocate it.
    // We could do the free before grabbing the lock if we arranged for
    // launch_next_task_in_new_thread() to do the pool manipulations
    // before calling chpl_thread_create(), but then we would also have
    // to be prepared to undo all those manipulations if we were unable
    // to create a thread.
    //
    tp->ptask = NULL;
    chpl_mem_free(ptask, 0, 0);

    //
    // finished task; decrement running count and increment idle count
    //
    assert(running_task_cnt > 0);
    running_task_cnt--;
    idle_thread_cnt++;

    //
    // wait for a not-yet-begun task to be present in the task pool
    //
    while (!task_pool_head) {
      chpl_thread_mutexUnlock(&threading_lock);

      while (!task_pool_head) {
        if (set_block_loc(0, idleTaskName)) {
          // all other tasks appear to be blocked
          struct timeval deadline, now;
          gettimeofday(&deadline, NULL);
          deadline.tv_sec += 1;
          do {
            chpl_thread_yield();
            if (!task_pool_head)
              gettimeofday(&now, NULL);
          } while (!task_pool_head
                   && (now.tv_sec < deadline.tv_sec
                       || (now.tv_sec == deadline.tv_sec
                           && now.tv_usec < deadline.tv_usec)));
          if (!task_pool_head)
            check_for_deadlock();
        }
        else {
          do {
            chpl_thread_yield();
          } while (!task_pool_head);
        }

        unset_block_loc();
      }

      chpl_thread_mutexLock(&threading_lock);
    }

    if (blockreport)
      progress_cnt++;

    assert(task_pool_head && !task_pool_head->begun);

    if (waking_thread_cnt > 0)
      waking_thread_cnt--;

    //
    // start new task; increment running count and remove task from pool
    // also add to task to task-table (structure in ChapelRuntime that keeps
    // track of currently running tasks for task-reports on deadlock or
    // Ctrl+C).
    //
    assert(queued_task_cnt > 0);
    queued_task_cnt--;
    idle_thread_cnt--;
    running_task_cnt++;
    ptask = task_pool_head;
    if (ptask->ltask) {
      ptask->ltask->ptask = NULL;
      // there is no longer any need to access the corresponding task
      // list entry so avoid any potential of accessing a node that
      // will eventually be freed
      ptask->ltask = NULL;
    }
    tp->ptask = ptask;
    ptask->begun = true;
    task_pool_head = task_pool_head->next;
    if (task_pool_head == NULL)  // task pool is now empty
      task_pool_tail = NULL;
    else {
      task_pool_head->prev = NULL;
    }

    // end critical section
    chpl_thread_mutexUnlock(&threading_lock);
  }
}


//
// When a thread is destroyed it calls this ending function.
//
static void thread_end(void)
{
  thread_private_data_t* tp;

  tp = (thread_private_data_t*) chpl_thread_getPrivateData();
  if (tp != NULL) {
    if (tp->lockRprt != NULL) {
      chpl_mem_free(tp->lockRprt, 0, 0);
      tp->lockRprt = NULL;
    }
    chpl_mem_free(tp, 0, 0);
    chpl_thread_setPrivateData(NULL);
  }
}


//
// run task in a new thread
// assumes at least one task is in the pool and threading_lock has already
// been acquired!
//
static void
launch_next_task_in_new_thread(void) {
  task_pool_p       ptask;
  static chpl_bool  warning_issued = false;

  if (warning_issued)  // If thread creation failed previously, don't try again
    return;

  if ((ptask = task_pool_head)) {
    if (chpl_thread_create(ptask)) {
      int32_t max_threads = chpl_thread_getMaxThreads();
      uint32_t num_threads = chpl_thread_getNumThreads();
      char msg[256];
      if (max_threads)
        sprintf(msg,
                "maxThreadsPerLocale is %"PRId32", but unable to create more than %d threads",
                max_threads, num_threads);
      else
        sprintf(msg,
                "maxThreadsPerLocale is unbounded, but unable to create more than %d threads",
                num_threads);
      chpl_warning(msg, 0, 0);
      warning_issued = true;
    } else {
      assert(queued_task_cnt > 0);
      queued_task_cnt--;
      running_task_cnt++;
      if (ptask->ltask) {
        ptask->ltask->ptask = NULL;
        // there is no longer any need to access the corresponding task
        // list entry so avoid any potential of accessing a node that
        // will eventually be freed
        ptask->ltask = NULL;
      }
      ptask->begun = true;
      task_pool_head = task_pool_head->next;
      if (task_pool_head == NULL)  // task pool is now empty
        task_pool_tail = NULL;
      else
        task_pool_head->prev = NULL;
    }
  }
}


// Schedule one or more tasks either by signaling an existing thread or by
// launching new threads if available
static void schedule_next_task(int howMany) {
  //
  // Reduce the number of new threads to be started, by the number that
  // are already looking for work and will find it very soon.  Try to
  // launch each remaining task in a new thread, up to the maximum number
  // of threads we are supposed to have.
  //
  if (idle_thread_cnt > waking_thread_cnt) {
    // increment waking_thread_cnt by the number of idle threads
    if (idle_thread_cnt - waking_thread_cnt >= howMany) {
      waking_thread_cnt += howMany;
      howMany = 0;
    } else {
      howMany -= (idle_thread_cnt - waking_thread_cnt);
      waking_thread_cnt = idle_thread_cnt;
    }
  }

  for (; howMany && chpl_thread_canCreate(); howMany--)
    launch_next_task_in_new_thread();
}


// create a task from the given function pointer and arguments
// and append it to the end of the task pool
// assumes threading_lock has already been acquired!
static task_pool_p add_to_task_pool(chpl_fn_p fp,
                                    void* a,
                                    chpl_bool serial,
                                    chpl_task_list_p ltask) {
  task_pool_p ptask =
    (task_pool_p) chpl_mem_alloc(sizeof(task_pool_t),
                                        CHPL_RT_MD_TASK_POOL_DESCRIPTOR,
                                        0, 0);
  ptask->id           = get_next_task_id();
  ptask->fun          = fp;
  ptask->arg          = a;
  ptask->serial_state = serial;
  ptask->ltask        = ltask;
  ptask->begun        = false;

  if (ltask) {
    ptask->filename = ltask->filename;
    ptask->lineno = ltask->lineno;
  } else {  /* Believe this happens only when an on-clause starts the task */
    ptask->filename = "<unknown>";
    ptask->lineno = 0;
  }

  ptask->next = NULL;

  if (task_pool_tail)
    task_pool_tail->next = ptask;
  else
    task_pool_head = ptask;
  ptask->prev = task_pool_tail;
  task_pool_tail = ptask;

  queued_task_cnt++;

  if (taskreport) {
    chpl_thread_mutexLock(&taskTable_lock);
    chpldev_taskTable_add(ptask->id,
                          ptask->lineno, ptask->filename,
                          (uint64_t) (intptr_t) ptask);
    chpl_thread_mutexUnlock(&taskTable_lock);
  }

  return ptask;
}


// Threads

uint32_t chpl_task_getNumThreads(void) {
  return chpl_thread_getNumThreads();
}

uint32_t chpl_task_getNumIdleThreads(void) {
  int numIdleThreads;

  // begin critical section
  chpl_thread_mutexLock(&threading_lock);

  numIdleThreads = idle_thread_cnt - waking_thread_cnt;

  // end critical section
  chpl_thread_mutexUnlock(&threading_lock);

  assert(numIdleThreads >= 0);
  return numIdleThreads;
}

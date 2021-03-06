/**************************************************************************
  Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
**************************************************************************/


#ifndef _chpltypes_H_
#define _chpltypes_H_

#include "sys_basic.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h> // for struct timeval

typedef enum {
  CHPL_TYPE_chpl_bool,
  CHPL_TYPE_chpl_bool8,
  CHPL_TYPE_chpl_bool16,
  CHPL_TYPE_chpl_bool32,
  CHPL_TYPE_chpl_bool64,
  CHPL_TYPE_enum,
  CHPL_TYPE_int8_t,
  CHPL_TYPE_int16_t,
  CHPL_TYPE_int32_t,
  CHPL_TYPE_int64_t,
  CHPL_TYPE_uint8_t,
  CHPL_TYPE_uint16_t,
  CHPL_TYPE_uint32_t,
  CHPL_TYPE_uint64_t,
  CHPL_TYPE__real32,
  CHPL_TYPE__real64,
  CHPL_TYPE__imag32,
  CHPL_TYPE__imag64,
  CHPL_TYPE__complex64,
  CHPL_TYPE__complex128,
  CHPL_TYPE_chpl_string,
  CHPL_TYPE_wide_string,
  CHPL_TYPE__cfile,
  CHPL_TYPE_chpl_task_list_p,
  CHPL_TYPE__timevalue,
  CHPL_TYPE_chpl_sync_aux_t,
  CHPL_TYPE_chpl_single_aux_t,
  CHPL_TYPE_chpl_taskID_t,
  CHPL_TYPE__symbol,
  CHPL_TYPE_CLASS_REFERENCE,
  CHPL_TYPE_DONE
} chplType;

typedef struct _chpl_fieldType {
  chplType type;
  size_t offset;
} chpl_fieldType;

#define nil 0 
typedef void* _nilType;
typedef void* _nilRefType;
typedef void* _chpl_object;
typedef void* _chpl_value;
typedef void* chpl_opaque;

#define nilRef 0

// macros for specifying the correct C constant type
#define INT8( i)   (i)
#define INT16( i)  (i)
#define INT32( i)  (i ## L)
#define INT64( i)  (i ## LL)
#define UINT8( i)  (i ## U)
#define UINT16( i) (i ## U)
#define UINT32( i) (i ## UL)
#define UINT64( i) (i ## ULL)


// Cuda does not suuport c99 bools
#ifndef ENABLE_GPU
typedef _Bool chpl_bool;
#else
typedef bool chpl_bool;
#endif

//typedef bool chpl_bool;
typedef int8_t chpl_bool8;
typedef int16_t chpl_bool16;
typedef int32_t chpl_bool32;
typedef int64_t chpl_bool64;

typedef void (*chpl_fn_p)(void*); // function pointer for runtime ftable
typedef int16_t chpl_fn_int_t;    // int type for ftable indexing

// It is tempting to #undef true and false and then #define them just to be sure
// they expand correctly, but future versions of the C standard may not allow this!
#ifndef false
#define false 0
#endif
#ifndef  true
#define  true 1
#endif

typedef float               _real32;
typedef double              _real64;
typedef float               _imag32;
typedef double              _imag64;
typedef struct __complex64 { _real32 re; _real32 im; } _complex64;
typedef struct __complex128 { _real64 re; _real64 im; } _complex128;
typedef const char*         chpl_string;
typedef int64_t              _symbol;

// macros for Chapel min/max -> C stdint.h or values.h min/max
#define MIN_INT8            INT8_MIN
#define MIN_INT16           INT16_MIN
#define MIN_INT32           INT32_MIN
#define MIN_INT64           INT64_MIN
#define MAX_INT8            INT8_MAX
#define MAX_INT16           INT16_MAX
#define MAX_INT32           INT32_MAX
#define MAX_INT64           INT64_MAX

#define MIN_UINT8           (0)
#define MIN_UINT16          (0)
#define MIN_UINT32          (0)
#define MIN_UINT64          (0)
#define MAX_UINT8           UINT8_MAX
#define MAX_UINT16          UINT16_MAX
#define MAX_UINT32          UINT32_MAX
#define MAX_UINT64          UINT64_MAX

// code gen just uses - MAX_FLOATxx #define MIN_FLOAT32         (-FLT_MAX)
//                                  #define MIN_FLOAT64         (-DBL_MAX)
#define MAX_FLOAT32         FLT_MAX
#define MAX_FLOAT64         DBL_MAX

static ___always_inline
int8_t ascii(chpl_string s) {
  return (int8_t) *s;
}

struct __chpl____wide_chpl_string;

//
// stopgap formatting
//
#undef printf
chpl_string chpl_format(chpl_string format, ...)
  __attribute__((format(printf, 1, 2)));
#define printf PRINTF_DEF

char* chpl_glom_strings(int numstrings, ...);

chpl_bool string_contains(chpl_string x, chpl_string y);
chpl_string string_copy(chpl_string x, int32_t lineno, chpl_string filename);
chpl_string chpl_wide_string_copy(struct __chpl____wide_chpl_string* x, int32_t lineno, chpl_string filename);
void chpl_string_widen(struct __chpl____wide_chpl_string* x, chpl_string from);
void chpl_comm_wide_get_string(chpl_string* local, struct __chpl____wide_chpl_string* x, int32_t tid, int32_t lineno, chpl_string filename);
chpl_string string_concat(chpl_string x, chpl_string y, int32_t lineno, chpl_string filename);
chpl_string string_index(chpl_string x, int i, int32_t lineno, chpl_string filename);
chpl_string string_select(chpl_string x, int low, int high, int32_t lineno, chpl_string filename);
chpl_string string_strided_select(chpl_string x, int low, int high, int stride, int32_t lineno, chpl_string filename);
int32_t chpl_string_compare(chpl_string x, chpl_string y);
int64_t string_length(chpl_string x);

int64_t real2int( _real64 f);       // return the raw bytes of the float
int64_t object2int( _chpl_object o);  // return the ptr

typedef struct timeval _timevalue;

_timevalue chpl_null_timevalue(void);
_timevalue chpl_now_timevalue(void);
int64_t chpl_timevalue_seconds(_timevalue t);
int64_t chpl_timevalue_microseconds(_timevalue t);
void chpl_timevalue_parts(_timevalue t, int32_t* seconds, int32_t* minutes, int32_t* hours, int32_t* mday, int32_t* month, int32_t* year, int32_t* wday, int32_t* yday, int32_t* isdst);

typedef int32_t chpl__class_id;

#endif

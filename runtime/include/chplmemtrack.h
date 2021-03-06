/**************************************************************************
  Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
**************************************************************************/


#ifndef _chplmemtrack_H_
#define _chplmemtrack_H_

#ifndef LAUNCHER

#include <stdio.h>
#include <stdlib.h>
#include "chpl-mem-desc.h"

// Memory tracking activated?
extern _Bool chpl_memTrack;

uint64_t chpl_memoryUsed(int32_t lineno, chpl_string filename);
void chpl_printMemStat(int32_t lineno, chpl_string filename);
void chpl_printMemTable(int64_t threshold, int32_t lineno, chpl_string filename);
void chpl_reportMemInfo(void);
void chpl_setMemFlags(chpl_bool memTrackConfig, chpl_bool memStatsConfig,
                      chpl_bool memLeaksConfig, chpl_bool memLeaksTableConfig,
                      uint64_t memMaxConfig, uint64_t memThresholdConfig,
                      chpl_string memLogConfig, chpl_string memLeaksLogConfig);
void chpl_track_malloc(void* memAlloc, size_t chunk, size_t number, size_t size, chpl_mem_descInt_t description, int32_t lineno, chpl_string filename);
void chpl_track_free(void* memAlloc, int32_t lineno, chpl_string filename);
void chpl_track_realloc1(void* memAlloc, size_t number, size_t size, chpl_mem_descInt_t description, int32_t lineno, chpl_string filename);
void chpl_track_realloc2(void* moreMemAlloc, size_t newChunk, void* memAlloc, size_t number, size_t size, chpl_mem_descInt_t description, int32_t lineno, chpl_string filename);

void chpl_startVerboseMem(void);
void chpl_stopVerboseMem(void);
void chpl_startVerboseMemHere(void);
void chpl_stopVerboseMemHere(void);

#else // LAUNCHER

#define chpl_setMemmax(value)
#define chpl_setMemstat()
#define chpl_setMemreport()
#define chpl_setMemtrack()

#endif // LAUNCHER

#endif

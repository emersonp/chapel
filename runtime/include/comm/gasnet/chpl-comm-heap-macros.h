/**************************************************************************
  Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
**************************************************************************/


#ifndef _comm_heap_macros_h_
#define _comm_heap_macros_h_


void chpl_comm_gasnet_help_register_global_var(int i, void* wide);

#define CHPL_HEAP_REGISTER_GLOBAL_VAR_EXTRA(i, wide) \
  chpl_comm_gasnet_help_register_global_var(i, (wide).addr);

#endif


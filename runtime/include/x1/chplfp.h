/**************************************************************************
  Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
**************************************************************************/


#ifndef _CHPL_FP_H_
#define _CHPL_FP_H_

#include <ieeefp.h>

#define isinf(x) (!finite(x) && !isnan(x))

#endif

#
# Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
#

RUNTIME_ROOT = ../../..
RUNTIME_SUBDIR = src/tasks/$(CHPL_MAKE_TASKS)

ifndef CHPL_MAKE_HOME
export CHPL_MAKE_HOME=$(shell pwd)/$(RUNTIME_ROOT)/..
endif

#
# standard header
#
include $(RUNTIME_ROOT)/make/Makefile.runtime.head

TASKS_OBJDIR = $(RUNTIME_OBJDIR)
include Makefile.share

TARGETS = $(TASKS_OBJS)

include $(RUNTIME_ROOT)/make/Makefile.runtime.subdirrules

FORCE:

#
# standard footer
#
include $(RUNTIME_ROOT)/make/Makefile.runtime.foot

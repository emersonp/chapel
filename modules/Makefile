#
# Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
#

ifndef CHPL_MAKE_HOME
export CHPL_MAKE_HOME=$(shell pwd)/..
endif

CHPL_MAKE_HOST_TARGET = --target
include $(CHPL_MAKE_HOME)/make/Makefile.base

SYS_MODULES_DIR=standard/gen/$(CHPL_MAKE_PLATFORM)-$(CHPL_MAKE_COMPILER)

SYS_CTYPES_MODULE=$(SYS_MODULES_DIR)/SysCTypes.chpl

default: all

all: $(SYS_CTYPES_MODULE)

clean: FORCE
	rm -rf $(SYS_MODULES_DIR)

clobber: FORCE
	rm -rf standard/gen

depend:

MAKE_SYS_BASIC_TYPES=$(CHPL_MAKE_HOME)/util/config/makeSysBasicTypes

$(SYS_CTYPES_MODULE): $(MAKE_SYS_BASIC_TYPES)
	mkdir -p $(@D)
	cd $(@D) && ($(MAKE_SYS_BASIC_TYPES) > $(@F) || rm -f $(@F))

FORCE:


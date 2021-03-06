ifndef CHPL_MAKE_HOME
export CHPL_MAKE_HOME=$(shell pwd)/../..
endif

CHPL_MAKE_HOST_TARGET = --target
include $(CHPL_MAKE_HOME)/make/Makefile.base

#
# set up the directories
#
LLVM_ABS_DIR = $(shell pwd)
LLVM_SRC_DIR = $(LLVM_ABS_DIR)/$(LLVM_SUBDIR)
LLVM_INSTALL_DIR = $(LLVM_ABS_DIR)/$(LLVM_INSTALL_SUBDIR)
LLVM_BUILD_DIR = $(LLVM_ABS_DIR)/$(LLVM_BUILD_SUBDIR)
LLVM_DIR = $(LLVM_ABS_DIR)
BINUTILS_GOLD_DIR = $(LLVM_ABS_DIR)/binutils

LLVM_FILE := $(LLVM_INSTALL_DIR)/include/llvm/Type.h

default: $(LLVM_FILE)

all: $(LLVM_FILE)

clean: FORCE
	cd $(LLVM_BUILD_DIR) && make clean

clobber: FORCE
	rm -r $(LLVM_INSTALL_DIR)

# -enable-debug-runtime --enable-debug-symbols creates huge binaries
#  so we by default do not include LLVM debugging information.
$(LLVM_FILE):
	if [ ! -d llvm ]; then ./update-llvm.sh; fi
	mkdir -p $(LLVM_BUILD_DIR)
	cd $(LLVM_BUILD_DIR) && $(LLVM_SRC_DIR)/configure CC='$(CC)' CXX='$(CXX)' --prefix=$(LLVM_INSTALL_DIR) --enable-optimized
	cd $(LLVM_BUILD_DIR) && $(MAKE)
	cd $(LLVM_BUILD_DIR) && $(MAKE) install
	
llvm: $(LLVM_FILE)

llvm-reconfig:
	cd $(LLVM_SRC_DIR) && sh autoconf/AutoRegen.sh

binutils:
	build-gold.sh

FORCE:



ifndef CHPL_MAKE_HOME
export CHPL_MAKE_HOME=$(shell pwd)/../..
endif

CHPL_MAKE_HOST_TARGET = --target
include $(CHPL_MAKE_HOME)/make/Makefile.base

TCMALLOC_ABS_DIR = $(shell pwd)
TCMALLOC_INSTALL_DIR=$(TCMALLOC_ABS_DIR)/$(TCMALLOC_INSTALL_SUBDIR)
TCMALLOC_BUILD_DIR = $(TCMALLOC_ABS_DIR)/$(TCMALLOC_BUILD_SUBDIR)
TCMALLOC_DIR = $(TCMALLOC_ABS_DIR)

CONFIG_FLAGS = --config-cache \
               --enable-shared=no \
               --disable-cpu-profiler --disable-heap-profiler \
               --disable-heap-checker --disable-debugalloc \
               --disable-libc-malloc-override \
               --prefix=$(TCMALLOC_INSTALL_DIR)


default: all

all: tcmalloc

clean: FORCE
	rm -rf $(TCMALLOC_BUILD_DIR)

clobber: FORCE
	rm -rf build install

tcmalloc: configure-tcmalloc build-tcmalloc install-tcmalloc

configure-tcmalloc: FORCE
	mkdir -p $(TCMALLOC_BUILD_DIR)
	cd $(TCMALLOC_BUILD_DIR) && $(TCMALLOC_SUBDIR)/configure CC='$(CC)' CXX='$(CXX)' $(CONFIG_FLAGS)

build-tcmalloc: FORCE
	cd $(TCMALLOC_BUILD_DIR) && XTPE_LINK_TYPE=dynamic $(MAKE)

install-tcmalloc: FORCE
	cd $(TCMALLOC_BUILD_DIR) && $(MAKE) install


FORCE:


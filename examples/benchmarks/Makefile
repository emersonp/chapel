
MAKEFLAGS = --no-print-directory

CHPL = chpl

default: hpcc lulesh ssca2

all: $(TARGETS)

clean: FORCE
	cd hpcc && $(MAKE) clean
	cd lulesh && $(MAKE) clean
	cd ssca2 && $(MAKE) clean

hpcc: FORCE
	cd hpcc && $(MAKE)

lulesh: FORCE
	cd lulesh && $(MAKE)

ssca2: FORCE
	cd ssca2 && $(MAKE)

FORCE:

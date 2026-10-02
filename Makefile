# ultimate-uci-oscar64 -- library checks
#
#   make check   compile every library function for C64 and C128, and check
#                that VERSION, the header's UII_LIB_VERSION and CHANGELOG.md
#                agree. Run before every commit to include/.
#   make smoke   build build/smoke64.prg: prints the library version and the
#                UCI identification on real hardware.
#   make clean   remove build/
#
# Override the compiler path with `make OSCAR64=/path/to/oscar64`.

OSCAR64 ?= $(HOME)/oscar64/bin/oscar64
CFLAGS  = -i=include -O2 -dNOFLOAT -n

LIBSRCS = $(wildcard include/*.c include/*.h)
VERSION = $(shell cat VERSION)

.PHONY: all check check-version smoke clean
all: check

check: check-version build/compile_all64.prg build/compile_all128.prg

check-version:
	@grep -q '^#define UII_LIB_VERSION "$(VERSION)"' include/ultimate_common_lib.h || \
		(echo "ERROR: UII_LIB_VERSION in include/ultimate_common_lib.h does not match VERSION ($(VERSION))" && false)
	@grep -q '^## \[$(VERSION)\]' CHANGELOG.md || \
		(echo "ERROR: CHANGELOG.md has no '## [$(VERSION)]' entry" && false)
	@echo "Version $(VERSION) consistent"

build/compile_all.c: tests/gen_compile_all.sh $(LIBSRCS)
	@mkdir -p build
	sh tests/gen_compile_all.sh > $@
	@echo "$$(grep -c '(void \*)uii_' $@) library functions"

build/compile_all64.prg: build/compile_all.c
	$(OSCAR64) $(CFLAGS) -i=include -tm=c64 -o=$@ $<

build/compile_all128.prg: build/compile_all.c
	$(OSCAR64) $(CFLAGS) -i=include -tm=c128 -o=$@ $<

smoke: build/smoke64.prg

build/smoke64.prg: tests/smoke.c $(LIBSRCS)
	@mkdir -p build
	$(OSCAR64) $(CFLAGS) -tm=c64 -o=$@ $<

clean:
	rm -rf build

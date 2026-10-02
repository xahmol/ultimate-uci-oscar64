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

.PHONY: all check check-version check-modplay-api smoke speedprobe clean
all: check

check: check-version build/compile_all64.prg build/compile_all128.prg build/compile_modplay64.prg check-modplay-api

check-version:
	@grep -q '^#define UII_LIB_VERSION "$(VERSION)"' include/ultimate_common_lib.h || \
		(echo "ERROR: UII_LIB_VERSION in include/ultimate_common_lib.h does not match VERSION ($(VERSION))" && false)
	@grep -q '^## \[$(VERSION)\]' CHANGELOG.md || \
		(echo "ERROR: CHANGELOG.md has no '## [$(VERSION)]' entry" && false)
	@echo "Version $(VERSION) consistent"

NOMODPLAY = $(filter-out include/ultimate_modplay_lib.h,$(wildcard include/ultimate_*_lib.h))

build/compile_all.c: tests/gen_compile_all.sh $(LIBSRCS)
	@mkdir -p build
	sh tests/gen_compile_all.sh $(NOMODPLAY) > $@
	@echo "$$(grep -c '(void \*)uii_' $@) library functions (+ MOD player, checked separately)"

build/compile_modplay64.prg: tests/compile_modplay.c $(LIBSRCS)
	@mkdir -p build
	$(OSCAR64) $(CFLAGS) -i=include -tm=c64 -o=$@ $<

# Every function declared in the MOD player header must be called by
# tests/compile_modplay.c.
check-modplay-api:
	@for f in $$(grep -oE '^[a-z][a-zA-Z_ *]*[ *](uii_[a-z_0-9]+)\(' include/ultimate_modplay_lib.h | grep -oE 'uii_[a-z_0-9]+'); do \
		grep -q "$$f(" tests/compile_modplay.c || { echo "ERROR: $$f missing in tests/compile_modplay.c"; exit 1; }; done
	@echo "MOD player API covered by tests/compile_modplay.c"

build/compile_all64.prg: build/compile_all.c
	$(OSCAR64) $(CFLAGS) -i=include -tm=c64 -o=$@ $<

build/compile_all128.prg: build/compile_all.c
	$(OSCAR64) $(CFLAGS) -i=include -tm=c128 -o=$@ $<

smoke: build/smoke64.prg

# Hardware test of the raster-timed speed probe (Ultimate 64, PAL).
speedprobe: build/speedprobe.prg

build/speedprobe.prg: tests/speed_probe.c $(LIBSRCS)
	@mkdir -p build
	$(OSCAR64) $(CFLAGS) -tm=c64 -o=$@ $<

build/smoke64.prg: tests/smoke.c $(LIBSRCS)
	@mkdir -p build
	$(OSCAR64) $(CFLAGS) -tm=c64 -o=$@ $<

clean:
	rm -rf build

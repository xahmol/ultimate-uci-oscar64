# ultimate-uci-oscar64

C library for the **Ultimate Command Interface (UCI)** of the Ultimate II+,
Ultimate II+L, Ultimate 64 and Commodore 64 Ultimate, for the
[Oscar64](https://github.com/drmortalwombat/oscar64) compiler. It covers
every command of firmware 3.15a's targets (DOS, control, network, SoftIEC
and HTTP) on the C64 and the C128, without dynamic allocation.

Not every function has been used on real hardware yet: those carry
`[UNTESTED]` in the headers, and section 20 of the manual lists the status
of each function (66 of 120 untested in 1.0.0, including the whole HTTP
target).

Based on the Ultimate II Dos Lib by Scott Hutter and Francesco Sblendorio
(https://github.com/xlar54/ultimateii-dos-lib). Adapted for Oscar64 by
Xander Mol, with fixes by Christian Gleissner. Licensed under the GNU GPL v3,
like the original.

The full reference is [`docs/UCILIB_MANUAL.md`](docs/UCILIB_MANUAL.md):
registers, protocol, every function, and how each firmware command is
covered.

## Files

| File | Contents |
|---|---|
| `include/ultimate_common_lib.h/.c` | Registers, protocol engine, detection, unlock, partitions, palette, version macros |
| `include/ultimate_dos_lib.h/.c` | File and directory I/O, REU transfer, drives, control commands, media helpers |
| `include/ultimate_time_lib.h/.c` | Real-time clock |
| `include/ultimate_network_lib.h/.c` | TCP/UDP client sockets |
| `include/ultimate_softiec_lib.h/.c` | SoftIEC target (firmware 3.15+) |
| `include/ultimate_http_lib.h/.c` | HTTP target (firmware 3.15+), untested |

Each header has a `#pragma compile(...)` for its `.c`, so a program only
includes the headers it needs; Oscar64 drops functions that are never
called.

## Using it in a project

Add the library as a git submodule, pinned to a release tag:

```
git submodule add https://github.com/xahmol/ultimate-uci-oscar64.git lib/ultimate-uci-oscar64
cd lib/ultimate-uci-oscar64 && git checkout v1.0.0 && cd ../..
git commit -m "Add ultimate-uci-oscar64 v1.0.0 as a submodule"
```

Add its `include/` folder to the compiler's include path and to the
Makefile's dependency list:

```make
UCILIB   = lib/ultimate-uci-oscar64/include
CFLAGS  += -i=$(UCILIB)
ALLSRCS += $(wildcard $(UCILIB)/*.c $(UCILIB)/*.h)
```

```c
#include "ultimate_common_lib.h"
#include "ultimate_dos_lib.h"

if (uii_wait_for_uci(10))          // unlock if needed, wait up to 10 s
    uii_identify();                // uii_data now holds the DOS version
```

Clone such a project with `git clone --recursive`, or run
`git submodule update --init` after a plain clone.

**Updating** to a newer release:

```
cd lib/ultimate-uci-oscar64 && git fetch --tags && git checkout v1.1.0 && cd ../..
git commit -am "Update ultimate-uci-oscar64 to v1.1.0"
```

Read [`CHANGELOG.md`](CHANGELOG.md) first: a new MAJOR version changes the
API. Programs can check the version at compile time with
`UII_LIB_VERSION_MAJOR`/`_MINOR`/`_PATCH`, or show `UII_LIB_VERSION`.

Don't edit the files inside the submodule. Fix or extend the library here,
release it, and update the submodule.

## Versions and releases

Semantic versioning: MAJOR for incompatible API changes, MINOR for new
functions, PATCH for fixes. To release:

1. Change `include/` and update `docs/UCILIB_MANUAL.md`.
2. Set the new version in `VERSION` and in the `UII_LIB_VERSION*` macros
   in `include/ultimate_common_lib.h`, and add a `CHANGELOG.md` entry.
3. `make check`: compiles every function for C64 and C128 and fails if the
   three version numbers disagree. Test on real hardware when the change
   touches the protocol.
4. Commit, tag `vX.Y.Z`, push with tags, and create a GitHub release.

## Build checks

```
make check   # every function for C64 and C128, plus the version check
make smoke   # build/smoke64.prg: prints the version and UCI identification
```

The compiler defaults to `~/oscar64/bin/oscar64`; override with
`make OSCAR64=/path/to/oscar64`.

## Known limitation

Projects that place library code in specific memory sections (for example
UBoot64-v2's bank switching) can't add `#pragma code(...)` lines to the
submodule's files. A hook for this is still to be designed.

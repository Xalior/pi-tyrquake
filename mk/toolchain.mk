# toolchain.mk — the make this build needs, the Arm GNU aarch64-none-elf cross
# toolchain, and the GNU tools circle-stdlib's configure needs on macOS.
#
# Include this at the top of any Makefile that compiles for the Pi.
#
# ---------------------------------------------------------------------------
# GNU make 4.0 or later
# ---------------------------------------------------------------------------
#
# macOS ships GNU make 3.81 as `make`; Homebrew installs a current one as
# `gmake`. 3.81 gets three things wrong that this build cannot survive:
#
#   It compares file timestamps to the SECOND. A source rewritten within the
#   same second its object was compiled in is never seen as newer, so the
#   object stays in the link carrying the older text — and a symbol list or an
#   image read off that build describes a file that no longer exists. It is
#   the one failure of the three that answers confidently and wrongly rather
#   than saying nothing. Make 4.x compares to the nanosecond, which APFS
#   records, and rebuilds.
#
#   A dependency file naming a header that has since moved or been deleted
#   stops it with exit 2 and no output at all — nothing names the file.
#
#   A -MG dependency on a header with no rule stops it the same silent way.
#
# The last two are also answered by writing dependency files with -MD -MP
# while compiling, which every makefile here does. The timestamp comparison is
# make's own and cannot be worked around from a makefile.
ifeq ($(filter 1.% 2.% 3.%,$(MAKE_VERSION)),$(MAKE_VERSION))
$(error this build needs GNU make 4.0 or later; this is '$(MAKE)' version '$(MAKE_VERSION)'. Homebrew installs one as gmake.)
endif

# ---------------------------------------------------------------------------
# Dry runs
# ---------------------------------------------------------------------------
#
# `make -n` EXECUTES any recipe line containing $(MAKE): make marks such a line
# always-run so a dry run can descend into the sub-make. A recursive target
# therefore builds for real under -n. Targets that recurse put NOT_DRY_RUN on
# their first line and refuse instead; the `+` prefix is what makes that line
# itself run under -n.
#
#	kernels:
#		+@$(NOT_DRY_RUN)
#		...
DRY_RUN     := $(findstring n,$(firstword -$(MAKEFLAGS)))
NOT_DRY_RUN  = $(if $(DRY_RUN),echo "$@: no dry run — this recipe drives sub-makes and make -n executes those for real." >&2; exit 1,:)

# ---------------------------------------------------------------------------
# C-only flags every port's own C recipes carry
# ---------------------------------------------------------------------------
#
# A port redirects upstream's objects out of the upstream tree, so it writes
# its own compile recipes and Circle's Rules.mk recipes never run. Whatever
# those recipes do not pass, the compiler decides — and the compiler's own
# default for C on this toolchain is gnu23, not the gnu99 Rules.mk names. The
# difference is silent until a clean rebuild, where C23 reading `f()` as
# `f(void)` turns a declaration the game has always had into a conflicting
# type. Each port states its C standard in its own C flags variable.
#
# static_assert: the assert.h on this include path is CIRCLE's, which comes
# first and defines assert and ASSERT_STATIC and nothing else. C11's
# static_assert macro is newlib's, two directories further along, and is
# therefore absent from every C file in a port — a missing macro that reads
# like a broken toolchain. This is the spelling newlib's own header uses.
# C++ needs none of it: there static_assert is a keyword, and defining a macro
# over it would be the reason the next build broke.
RAPI_CFLAGS = -Dstatic_assert=_Static_assert

# ---------------------------------------------------------------------------
# The cross toolchain
# ---------------------------------------------------------------------------
#
# The cross compiler is looked for on PATH first, so a machine that already
# has it installed (a stranger's, a CI runner's) is left alone. Failing that,
# two places are searched, in order:
#
#   $RAPI_TOOLCHAIN_DIR — set it when the toolchain lives somewhere else
#                         entirely, which is the case for anyone consuming
#                         this repository from outside it.
#   toolchains/         — a copy unpacked into this repository.
#
# Either may be the unpacked toolchain itself or a directory holding one or
# more unpacked releases:
#
#   <dir>/arm-gnu-toolchain-<release>-aarch64-none-elf/bin/
#   <dir>/bin/
#
# That directory is not tracked in git — it holds a couple of gigabytes of
# vendor binaries. Download release 15.2.Rel1 for the aarch64-none-elf
# target, matching the machine you build ON, from
# https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads and
# unpack it there, or put its bin/ on your PATH. A symbolic link to a copy
# unpacked elsewhere works just as well.
#
# Build from the repository root — `make rpi5`, not `make -C host`. macOS
# ships GNU make 3.81, which passes a makefile's exported variables to its
# recipes but NOT to the $(shell ...) function. Circle's Rules.mk finds the
# compiler's support objects with $(shell $(CPP) -print-file-name=...), so
# in a make started without the toolchain already in its environment those
# lookups come back empty while every compile succeeds — and the failure
# surfaces much later as the linker reporting it "cannot find" a file with
# no name. Running from the root avoids it: the PATH exported here reaches
# the sub-make as an ordinary environment variable, because the sub-make is
# started from a recipe.

TOOLCHAIN_MK_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT        := $(abspath $(TOOLCHAIN_MK_DIR)/..)

# Where to look, in order, when the compiler is not already on PATH:
# RAPI_TOOLCHAIN_DIR if the environment names one, then this repository's own
# toolchains/. Either may be the unpacked toolchain itself (it has a bin/) or
# a directory holding one or more unpacked releases.
TOOLCHAIN_SEARCH := $(RAPI_TOOLCHAIN_DIR) $(REPO_ROOT)/toolchains

ifeq ($(shell command -v aarch64-none-elf-gcc 2>/dev/null),)
TOOLCHAIN_BIN := $(firstword \
	$(wildcard $(addsuffix /arm-gnu-toolchain-*-aarch64-none-elf/bin,$(TOOLCHAIN_SEARCH))) \
	$(wildcard $(addsuffix /bin,$(TOOLCHAIN_SEARCH))))
ifneq ($(TOOLCHAIN_BIN),)
export PATH := $(TOOLCHAIN_BIN):$(PATH)
endif
endif

# GNU getopt for circle-stdlib's configure: macOS's BSD getopt drops long
# options, which lands as "Error: Invalid toolchain prefix".
GETOPT_BIN := $(firstword $(wildcard /opt/homebrew/opt/gnu-getopt/bin /usr/local/opt/gnu-getopt/bin))
ifneq ($(GETOPT_BIN),)
export PATH := $(GETOPT_BIN):$(PATH)
endif

# A bash 5 for `bash ./configure`: macOS ships 3.2, which has no mapfile.
BASH5_BIN := $(firstword $(wildcard /opt/homebrew/bin/bash /usr/local/bin/bash))
ifneq ($(BASH5_BIN),)
export PATH := $(patsubst %/,%,$(dir $(BASH5_BIN))):$(PATH)
endif

.PHONY: check-toolchain
check-toolchain:
	@command -v aarch64-none-elf-g++ >/dev/null 2>&1 || { \
		echo "aarch64-none-elf-g++ not found."; \
		echo "Put the Arm GNU aarch64-none-elf toolchain on your PATH, or"; \
		echo "unpack it into $(REPO_ROOT)/toolchains/, or set RAPI_TOOLCHAIN_DIR"; \
		echo "to where it lives — see mk/toolchain.mk."; \
		exit 1; }
	@echo "  TOOLCHAIN $$(command -v aarch64-none-elf-g++)"
	@aarch64-none-elf-g++ --version | head -1

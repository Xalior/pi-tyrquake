#
# TyrQuake as a bootable Raspberry Pi kernel image.
#
# One board per invocation (RAPI_BOARD, default rpi5). The board selects
# three things at once: the circle-stdlib world the kernel is compiled and
# linked against, the shim archive built against that world, and — through
# the world's Config.mk — the CPU tuning and RASPPI value. Each board's
# newlib and libc++ are built for its own architecture, so an image is
# never portable between boards.
#
#   make                     the default board's image
#   make RAPI_BOARD=rpi3     that board's image
#   make clean-board         drop just this board's build/<board>/ tree
#
# Everything this invocation generates lands under build/<board>/: the
# kernel's own objects, every TyrQuake object, and the final
# .elf/.map/.lst/.img. The object names are shared across boards but their
# contents are not (different -mcpu, different -DRASPPI, different world),
# so a flat build directory would silently relink one board's objects into
# another board's kernel — a corrupt image with no build error. A directory
# per board keeps all three isolated: no `make clean` between boards, and
# three boards can build at the same time without touching each other.
#
# TyrQuake's own source tree (../tyrquake) is never written into: it is
# read-only input, and every object it produces is redirected here.
#
# Prerequisite: the board's world and shim archive exist —
# `make -C ../circle-libsdl2 deps`, or `make deps` at the repository root.
#

include ../mk/toolchain.mk

BOARDS      = rpi3 rpi4 rpi5
RAPI_BOARD ?= rpi5

ROOT = ..
SHIM = $(ROOT)/circle-libsdl2
TYR  = $(ROOT)/tyrquake

OBJDIR = build/$(RAPI_BOARD)

# Circle's own image names, which the Raspberry Pi firmware selects between
# from the model sections of config.txt: the Pi 3 world boots kernel8.img,
# the Pi 4 world kernel8-rpi4.img, the Pi 5 world kernel_2712.img.
IMAGE_rpi3 = kernel8
IMAGE_rpi4 = kernel8-rpi4
IMAGE_rpi5 = kernel_2712
IMAGE = $(IMAGE_$(RAPI_BOARD))

# Before the world is included, so an unknown board is named as an unknown
# board. After the include it arrives as a missing Config.mk under a directory
# whose name nobody recognises.
ifeq ($(filter $(RAPI_BOARD),$(BOARDS)),)
$(error unknown RAPI_BOARD '$(RAPI_BOARD)' — expected one of: $(BOARDS))
endif

# The board's circle-stdlib world. A `make deps` that has not been run, or has
# not finished, leaves this file absent; make would then report that it has no
# rule to make a Config.mk, which describes the symptom and not the cause.
# WHERE THE BUILT WORLD COMES FROM.
#
# The default is this game's own nested shim, so a clone of this repository
# alone builds and uses its own worlds and nothing needs setting. A parent
# that keeps ONE set of built worlds for several games — they are gigabytes
# each, and identical when the pin and the configuration match — points
# CIRCLE_WORLDS at them.
CIRCLE_WORLDS ?= $(SHIM)
WORLD = $(CIRCLE_WORLDS)/circle-stdlib-$(RAPI_BOARD)
ifeq ($(wildcard $(WORLD)/Config.mk),)
$(error the $(RAPI_BOARD) circle-stdlib world is not configured ($(WORLD)/Config.mk is missing) — run 'make deps-$(RAPI_BOARD)' at the repository root)
endif

include $(WORLD)/Config.mk

TARGET = $(OBJDIR)/$(IMAGE)

# Stated explicitly because the first rule this file sees comes from an
# included makefile, and that would otherwise decide the default goal.
.DEFAULT_GOAL := image

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

# The kernel wrapper, the syscall layer and this port's own SDL layer: C++,
# and this project's own code.
HOST_SRCS = main.cpp kernel.cpp defaults.cpp circle_syscalls.cpp \
	circle_stubs.cpp
HOST_OBJS = $(addprefix $(OBJDIR)/,$(HOST_SRCS:.cpp=.o))

# TyrQuake's source list for ONE of its five programs: `tyr-quake`, the
# NetQuake client with the SOFTWARE renderer. Upstream's Makefile calls that
# combination NQSW and assembles it from six lists, which are reproduced here
# in the same groups and the same order so the two can be compared by eye.
#
# The other four programs are the OpenGL client, the two QuakeWorld clients
# and the QuakeWorld server. The OpenGL ones need a GL implementation that
# does not exist on this board; the QuakeWorld ones need a network stack.
#
# Upstream searches common/ first and then NQ/, so a file's directory here is
# the one upstream's own build would have found.

TYR_COMMON = $(addprefix common/, \
	buildinfo.c cmd.c common.c crc.c cvar.c mathlib.c model.c rb_tree.c \
	shell.c zone.c)

TYR_CLIENT = \
	common/alias_model.c common/cd_common.c common/cl_input.c \
	common/console.c common/developer.c common/keys.c common/menu.c \
	common/pcx.c common/r_lerp.c common/r_efrag.c common/r_light.c \
	common/r_model.c common/r_part.c common/screen.c common/snd_dma.c \
	common/snd_mem.c common/snd_mix.c common/snd_music.c \
	common/sprite_model.c common/vid_mode.c common/wad.c \
	NQ/cl_demo.c NQ/cl_main.c NQ/cl_parse.c NQ/cl_tent.c NQ/sbar.c \
	NQ/view.c

TYR_SERVER = $(addprefix common/, \
	pr_cmds.c pr_edict.c pr_exec.c sv_move.c sv_phys.c world.c) \
	NQ/sv_main.c NQ/sv_user.c

TYR_NETQUAKE = $(addprefix NQ/, \
	chase.c host.c host_cmd.c net_common.c net_loop.c net_main.c)

TYR_SOFTWARE = $(addprefix common/, \
	d_edge.c d_fill.c d_init.c d_modech.c d_part.c d_polyse.c d_scan.c \
	d_sky.c d_sprite.c d_surf.c d_vars.c draw.c r_aclip.c r_alias.c \
	r_bsp.c r_draw.c r_edge.c r_main.c r_misc.c r_sky.c r_sprite.c \
	r_surf.c r_vars.c)

# The drivers. Upstream picks one of each from its VID_TARGET, IN_TARGET,
# SND_TARGET and CD_TARGET options; these are the ones this port selects and
# every one of them is upstream's own file, chosen and not modified.
#
#   vid_sdl.c, in_sdl.c, snd_sdl.c, sdl_common.c
#           the SDL video, input and sound drivers. TyrQuake's SDL video
#           driver is the software-rendered one: the game draws into an
#           8-bit buffer of its own and this file converts it into a
#           streaming texture once a frame.
#   cd_null.c
#           the no-CD-audio driver. There is no optical drive on a
#           Raspberry Pi, and Quake's music tracks came from the game CD.
#   net_none.c
#           upstream's loopback-only network driver, the one with no
#           datagram transport under it. Chosen over net_bsd.c because
#           circle-libsdl2 has no network stack behind it and the datagram
#           driver is written against BSD sockets. The game is therefore
#           single player, which is what the loopback driver has always
#           meant.
#   sys_unix.c
#           the operating-system layer: the program's entry point, the
#           clock, the frame loop and the file timestamps. It is written to
#           plain POSIX, which is what the C library on this board offers,
#           so it is used unchanged — see this port's own layer in
#           circle_stubs.cpp for the handful of calls newlib does not carry.
TYR_DRIVERS = \
	common/vid_sdl.c common/sdl_common.c common/in_sdl.c \
	common/snd_sdl.c common/cd_null.c common/sys_unix.c NQ/net_none.c

TYR_SRCS = $(TYR_COMMON) $(TYR_CLIENT) $(TYR_SERVER) $(TYR_NETQUAKE) \
	$(TYR_SOFTWARE) $(TYR_DRIVERS)
TYR_OBJS = $(addprefix $(OBJDIR)/tq/,$(TYR_SRCS:.c=.o))

# This directory's own C sources: the ones that have to be compiled as part
# of the game rather than as part of the kernel, because they use the game's
# own headers and types. They are built with the same compiler and the same
# flags every one of the game's files gets.
HOST_C_SRCS = net_ban.c
HOST_C_OBJS = $(addprefix $(OBJDIR)/,$(HOST_C_SRCS:.c=.o))

OBJS = $(HOST_OBJS) $(HOST_C_OBJS) $(TYR_OBJS)

STANDARD = -std=gnu++17

# Stated, not inherited: this is Circle's own default, written down so the
# build's optimization level is a fact in the file rather than a forensic
# reconstruction.
OPTIMIZE = -O2

# Dependency files are written as a side effect of compiling rather than by
# rules of their own, and Circle's Rules.mk is told to keep its hands off
# (CHECK_DEPS) so its `-M -MG` .d rules are not defined for these objects.
#
# -MG lists a header make has no rule for as a prerequisite of the object,
# which stops the build; GNU make 3.81 stops with exit 2 and prints not one
# line saying why, which is why this build asks for 4.0 in mk/toolchain.mk. A
# .d left over from an earlier build naming a header that has since moved or
# been deleted stops it the same way, and is why "delete the board's object
# directory" kept being the cure.
#
# -MD writes the dependency file alongside the object it belongs to, so the
# two can never disagree about the flags that produced them, and -MP adds a
# rule with no recipe for every header named, so a header that disappears
# means a rebuild instead of a dead stop.
DEPS       = $(OBJS:.o=.d)
CHECK_DEPS = 0
DEPFLAGS   = -MD -MP

include $(CIRCLEHOME)/Rules.mk

# The world must be the multicore one circle-libsdl2 configures: the shim's
# core-split machinery cannot exist without it, and an object compiled
# against a single-core world links without complaint and then misbehaves.
# Every object here inherits the flag through Rules.mk's CPPFLAGS, so this
# check covers the whole build.
ifeq ($(findstring ARM_ALLOW_MULTI_CORE,$(DEFINE)),)
$(error the payload requires the multicore circle-stdlib world — build it with 'make -C ../circle-libsdl2 deps')
endif

# WHERE THIS GAME LIVES ON THE CARD. One card carries several games, and two
# of them writing a `config.cfg` into the FAT root would each silently
# overwrite the other's. Every game therefore keeps to a directory of its own,
# and nothing of this port's ever touches the root.
#
# It is a build-time constant, not a boot switch: a card's layout is decided
# when the card is staged, and a game that could be relocated at boot would
# only mean two places to look for the same file.
#
# The path is used ONLY by this port's own host layer and by one upstream
# build knob that already exists for exactly this purpose — QBASEDIR, the
# directory TyrQuake looks for `id1/` under. Upstream is never patched: it is
# pointed.
RAPI_GAME_DIR ?= /games/tyrquake
DEFINE += -DRAPI_GAME_DIR='"$(RAPI_GAME_DIR)"'

# ---------------------------------------------------------------------------
# Upstream's own build-configuration defines
# ---------------------------------------------------------------------------
#
# Every one of these is a switch upstream's Makefile sets for the same build.
#
#   NQ_HACK        selects the NetQuake flavour of the shared source. The
#                  QuakeWorld flavour is QW_HACK, and the two are mutually
#                  exclusive.
#   NDEBUG         upstream's non-debug build. It also removes the assert()
#                  in the sound callback.
#   ELF            upstream sets this for every unix target. It selects the
#                  ELF spellings in the assembly-language headers.
#   QBASEDIR       where the game looks for `id1/`. Read through upstream's
#                  own stringify(), so it is an unquoted path here, exactly
#                  as upstream passes it.
#   TYR_VERSION      the release string the game prints and stamps into demos.
#   TYR_VERSION_TIME its timestamp, in seconds since 1970.
#
# The version pair is a constant rather than something read out of git.
# Upstream's Makefile asks `git describe` and falls back to the modification
# time of a file in the working tree, and neither answer survives a fresh
# clone: the submodule is checked out at a detached release tag, and file
# timestamps are whatever the clone made them. The pinned release and its own
# commit timestamp are the honest answer, and they change when the pin does.
TYR_VERSION      = 0.71
TYR_VERSION_TIME = 1662459833

#   _DEFAULT_SOURCE  asks the C library for its full default feature set.
#                    Newlib hides the POSIX declarations behind a
#                    feature-test macro and, on this configuration, none is
#                    set by default — so <time.h> declares no nanosleep, and
#                    the game's own operating-system layer stops on an
#                    implicit declaration before the linker is ever reached.
#                    This is the least restrictive of the macros that turn
#                    them on: it adds declarations and removes none.
TYR_DEFINE = -DNQ_HACK -DNDEBUG -DELF -D_DEFAULT_SOURCE \
	-DQBASEDIR=$(RAPI_GAME_DIR) \
	-DTYR_VERSION=$(TYR_VERSION) \
	-DTYR_VERSION_TIME=$(TYR_VERSION_TIME)LL

# TyrQuake writes `#include "SDL.h"`, the spelling a desktop sdl2-config
# include path provides, so the shim's SDL2/ directory goes on the path in its
# own right alongside the SDL2/-prefixed one this project's own code uses.
#
# The game's own directories are on the path with -iquote, the way upstream
# puts them there. That is not a detail: the game carries headers named
# list.h, shell.h and sys.h, and -iquote confines them to the game's own
# `#include "..."` lines so they can never shadow a C library header of the
# same name for anything else in the build.
#
# include/ holds the headers this port supplies to the game's build: the
# window-icon header upstream's own build generates rather than ships, and
# the C library headers this board's newlib does not have but the game's
# operating-system layer includes. Each one says in itself why it is there.
# It is a -I rather than an -iquote because they answer angle-bracket
# includes, and it comes before the C library's own directory because one of
# them has to be found instead of the C library's.
INCLUDE := -I $(SHIM)/include -I $(SHIM)/include/SDL2 \
	-iquote $(TYR)/include -iquote $(TYR)/external -iquote $(TYR)/NQ \
	-I $(CURDIR)/include \
	$(CIRCLE_STDLIB_INCLUDES) $(INCLUDE)

# The C sources are C99, which is what upstream's build asks for. It is
# stated here rather than added to CFLAGS because Rules.mk folds CFLAGS into
# the C++ flags as well, and a C standard handed to the C++ compiler is a
# warning on every C++ file in the build. $(STANDARD) above covers those.
TYR_CFLAGS = -std=gnu99 $(RAPI_CFLAGS) $(TYR_DEFINE) $(CFLAGS)

# The shim comes in whole, every member, rather than only the members the
# link happens to need. An object file linked straight into the kernel beats
# an archive member of the same name and does it silently, so a stub in
# circle_stubs.cpp left behind after the library implemented the real thing
# would quietly win — the game would keep calling the stub and nothing would
# say so. Pulling the archive in whole turns that into a duplicate-symbol
# error at link time, which is the only moment it can still be cheap to fix.
SHIM_LIB = $(SHIM)/libSDL2-$(RAPI_BOARD).a

LIBS = --whole-archive $(SHIM_LIB) --no-whole-archive \
	$(CIRCLE_STDLIB_LIBS)

# The shim archive is built in circle-libsdl2 and never here — it is a shared
# dependency, and a rule here would rebuild it at whatever board and crossing
# count this makefile happens to be running with. This rule exists only for the
# case where it is absent: it runs then, and only then, because a target with a
# recipe and no prerequisites is up to date as soon as the file exists.
$(SHIM_LIB):
	@echo "$@ is missing: the $(RAPI_BOARD) shim archive has not been built."
	@echo "Run: $(MAKE) -C $(SHIM) libSDL2-$(RAPI_BOARD).a BOARD=$(RAPI_BOARD)"
	@exit 1

# ---------------------------------------------------------------------------
# Filesystem syscalls, redirected at link time
# ---------------------------------------------------------------------------
#
# The game runs on the application core, and newlib's Circle glue drives
# FatFs and the SD card straight from wherever it is called — which off core
# 0 is illegal. circle_syscalls.cpp puts the shim's any-core I/O service
# underneath the C library instead; these flags are what put it there.
#
# --wrap rather than redefinition, because redefinition cannot work here:
# the glue defines _open, _read, _write, _close, _lseek, _fstat, opendir,
# readdir, closedir, dup and more in a SINGLE object file. Redefining part
# of that set either collides at link time or, worse, leaves the originals
# linked in beside the replacements with no diagnostic. --wrap renames the
# references instead and leaves the vendored glue untouched — and it is what
# keeps __real_* reachable, which is how the service performs the real work
# once it has reached core 0.
#
# Every name here must also appear in circle_syscalls.cpp: a wrap with no
# __wrap_ definition is an undefined reference, so the link is the check.
WRAPPED_SYSCALLS = \
	_open _close _read _write _lseek _fstat _stat lstat _unlink _rename \
	_fcntl mkdir chdir getcwd dup dup2 \
	opendir readdir closedir rewinddir

# The link is driven by the cross ld directly (Circle's Rules.mk sets
# LD = <prefix>ld for GCC builds), so these are plain linker options, not
# compiler-driver -Wl, ones.
LDFLAGS += $(addprefix --wrap=,$(WRAPPED_SYSCALLS))

# ---------------------------------------------------------------------------
# Compilation
# ---------------------------------------------------------------------------

# This project's own objects, board-scoped. These more-specific rules win
# over Rules.mk's in-place %.o/%.d, so nothing is written beside the source.
$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@echo "  CPP   $@"
	@$(CPP) $(CPPFLAGS) $(DEPFLAGS) -c -o $@ $<

# This directory's own C sources, built with the game's flags — see
# HOST_C_SRCS above for why they are not built with the kernel's.
$(OBJDIR)/%.o: %.c | $(OBJDIR)
	@echo "  CC    $@"
	@$(CC) $(TYR_CFLAGS) $(DEPFLAGS) -c -o $@ $<

# TyrQuake's objects, mirroring its source tree under $(OBJDIR)/tq/. The
# source tree itself is the upstream submodule, and is never written to.
$(OBJDIR)/tq/%.o: $(TYR)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC    $@"
	@$(CC) $(TYR_CFLAGS) $(DEPFLAGS) -c -o $@ $<

# The game's operating-system layer, which is the one file in the build that
# needs anything said about it. Two things are said, and both are said here
# rather than in the game's source, which is never edited.
#
# ITS main() IS NOT THIS PROGRAM'S. main() belongs to the Circle kernel
# (main.cpp in this directory), which calls the game once the board is up, so
# the entry point is renamed through the preprocessor for this one
# translation unit and the kernel calls the new name. Upstream does the same
# thing to the same file for its macOS application bundle, with
# -Dmain=SDL_main.
#
# IT REACHES ONE FUNCTION THE C LIBRARY DOES NOT DECLARE. -include puts this
# port's declarations in front of the file's own includes; the header says
# which function and why.
SYS_UNIX_CFLAGS = -Dmain=tyrquake_main -include $(CURDIR)/include/rapi_posix_missing.h

$(OBJDIR)/tq/common/sys_unix.o: $(TYR)/common/sys_unix.c
	@mkdir -p $(dir $@)
	@echo "  CC    $@ (entry point renamed)"
	@$(CC) $(TYR_CFLAGS) $(SYS_UNIX_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(OBJDIR):
	@mkdir -p $(OBJDIR)

-include $(DEPS)

# The image link rule. sdl-app.mk lets the caller choose the linker script,
# so this build supplies its own: the shim's script with room made at image
# offset 0x800 for the patchable-defaults block. Everything else about the
# script — including the TLS-section adjacency binutils 2.44 and later
# require — is inherited from the shim's.
SDL_APP_LDSCRIPT = $(CURDIR)/tyrquake-defaults.ld
include $(SHIM)/sdl-app.mk

# The other half of the guarantee the linker script's ASSERTs begin: check
# the BYTES of the finished image, not just the link. A pre-boot writer
# refuses to stamp an image whose magic is not at 0x800, so an image that
# lost it would boot and then quietly ignore everything ever sent to it —
# including --rapi-debug-uart, which is the channel used to drive the board
# when nobody is at the bench. Better to have no image than that one.
.PHONY: image
image: $(TARGET).img
	@if [ "`dd if=$(TARGET).img bs=4 skip=512 count=1 2>/dev/null`" = "PM8D" ]; \
	then \
		echo "  GATE  $(TARGET).img defaults block at 0x800: present"; \
	else \
		echo "  GATE  $(TARGET).img: defaults block ABSENT at 0x800 — image deleted"; \
		rm -f $(TARGET).img; \
		exit 1; \
	fi

# Board-scoped clean: drop just this board's tree, leaving the other two
# boards' images and objects untouched.
.PHONY: clean-board
clean-board:
	rm -rf $(OBJDIR) build/$(RAPI_BOARD)-stub

# This board from nothing. An incremental build is a decision made from
# timestamps, and a timestamp says when a file was written rather than what is
# in it; anything read off the resulting image describes what the last build
# happened to put there. Take a measurement that has to be right off one of
# these.
.PHONY: rebuild
rebuild:
	+@$(NOT_DRY_RUN)
	@$(MAKE) clean-board RAPI_BOARD=$(RAPI_BOARD)
	@$(MAKE) image RAPI_BOARD=$(RAPI_BOARD)

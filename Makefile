#
# pi-tyrquake — TyrQuake as a bootable bare-metal Raspberry Pi image.
#
#   make check-toolchain     report the cross compiler this build will use
#   make deps                the three circle-stdlib worlds and the shim
#                            archives built against them (long: the worlds
#                            build newlib and libc++ from source)
#   make deps-rpi4           the same for one board only, for a machine that
#                            cannot hold three worlds at once
#   make rpi5 | rpi4 | rpi3  one board's kernel image
#   make kernels             all three, built in parallel
#   make verify              truth-gate: every image exists and is non-empty
#   make media               download the freely redistributable game data
#                            into media/
#   make netboot             stage the Pi 5 image and its boot configuration
#                            into build/netboot-rpi5/
#   make card                stage the whole card into build/sd-card/, copying
#                            in whatever media/ holds and naming what it does
#                            not. It never downloads anything
#   make clean-boards        drop every board's build tree
#
# The three boards never share mutable state: each has its own circle-stdlib
# world, its own shim archive and its own object directory, so building them
# at the same time is safe and building one never disturbs another.
#
# The libc++ sources every world is built from are one immutable git tag, and
# CIRCLE_LLVM says where that checkout lives. The default puts it beside this
# repository, which is right for a plain clone and for a CI runner. Point
# several projects at one directory to fetch it once for all of them:
#
#   make deps CIRCLE_LLVM=/path/to/circle-llvm
#

include mk/toolchain.mk

# Stated explicitly because the first rule this file sees comes from an
# included makefile, and that would otherwise decide the default goal.
.DEFAULT_GOAL := kernels

BOARDS ?= rpi3 rpi4 rpi5

IMAGE_rpi3 = kernel8.img
IMAGE_rpi4 = kernel8-rpi4.img
IMAGE_rpi5 = kernel_2712.img

.PHONY: deps kernels rebuild verify media netboot card clean-boards $(BOARDS)
.PHONY: $(addprefix deps-,$(BOARDS)) $(addprefix rebuild-,$(BOARDS))

deps:
	+@$(NOT_DRY_RUN)
	$(MAKE) -C circle-libsdl2 deps

# One board's dependencies: its own circle-stdlib world and the shim archive
# built against it. A machine with a small disk — a CI runner, most obviously
# — builds one board at a time and keeps only that board's world.
# Written as a static pattern rule over the board list rather than a plain
# pattern rule: these targets are phony, and make does not apply pattern rules
# to phony targets — it would quietly answer "nothing to be done" and leave
# the world unbuilt.
# Quake's renderer allocates its edge and surface arrays with alloca on every
# frame it draws, and upstream says it wants at least a megabyte of stack to
# do that — a figure written for 32-bit pointers, so the same arrays are
# around 1.7 times larger here. Circle's default 128 KB a core is not enough
# for the first frame of real geometry, and there is no guard page to catch
# the overrun: the core simply writes into the next core's stack.
CIRCLE_KERNEL_STACK_SIZE = 0x200000

$(addprefix deps-,$(BOARDS)): deps-%:
	+@$(NOT_DRY_RUN)
	$(MAKE) -C circle-libsdl2 world BOARD=$* \
		CIRCLE_KERNEL_STACK_SIZE=$(CIRCLE_KERNEL_STACK_SIZE)
	$(MAKE) -C circle-libsdl2 libSDL2-$*.a BOARD=$*

$(BOARDS): check-toolchain
	+@$(NOT_DRY_RUN)
	$(MAKE) -C host RAPI_BOARD=$@

# All three at once. Each sub-make owns a different world and a different
# output directory, so there is nothing for them to collide on.
#
# Each board is waited for BY PID, and its status kept. A bare `wait` reports
# only that the shell has no children left — it is success whatever the jobs
# did — so a board that failed to build would leave this target reporting
# success, and the truth-gate would then pass the board's PREVIOUS image,
# still on disk.
kernels: check-toolchain
	+@$(NOT_DRY_RUN)
	@pids=; fail=0; \
	for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b & pids="$$pids $$!"; done; \
	for p in $$pids; do wait $$p || fail=1; done; \
	exit $$fail

# One board from nothing: its build tree is removed before the build, so no
# object can be inherited from a previous one. Written as a static pattern rule
# over the board list for the same reason deps-% is.
$(addprefix rebuild-,$(BOARDS)): rebuild-%: check-toolchain
	+@$(NOT_DRY_RUN)
	$(MAKE) -C host RAPI_BOARD=$* rebuild

# All three from nothing, in parallel, waited for by PID exactly as kernels is.
rebuild: check-toolchain
	+@$(NOT_DRY_RUN)
	@pids=; fail=0; \
	for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b rebuild & pids="$$pids $$!"; done; \
	for p in $$pids; do wait $$p || fail=1; done; \
	exit $$fail

# Truth-gate: ask the filesystem, not the exit codes. An image that is
# missing, empty, or does not carry the defaults block at offset 0x800 fails
# here even if the build claimed success.
#
# What this cannot tell you is whether the image was built from the sources as
# they now stand. That is a question about the build, not about the file, and
# `make rebuild` is the only answer to it.
verify:
	@fail=0; \
	for b in $(BOARDS); do \
		case $$b in \
			rpi3) img=host/build/rpi3/$(IMAGE_rpi3) ;; \
			rpi4) img=host/build/rpi4/$(IMAGE_rpi4) ;; \
			rpi5) img=host/build/rpi5/$(IMAGE_rpi5) ;; \
		esac; \
		if [ ! -s "$$img" ]; then \
			echo "  FAIL  $$img missing or empty"; fail=1; \
		elif [ "`dd if=$$img bs=4 skip=512 count=1 2>/dev/null`" != "PM8D" ]; then \
			echo "  FAIL  $$img has no defaults block at 0x800"; fail=1; \
		else \
			echo "  OK    $$img ($$(wc -c < $$img | tr -d ' ') bytes, defaults block present)"; \
		fi; \
	done; \
	exit $$fail

# ---------------------------------------------------------------------------
# Game data
# ---------------------------------------------------------------------------
#
#   media/           what `make media` downloads. Gitignored, never shipped,
#                    and never part of a build.
#   build/sd-card/   what `make card` stages. It copies from media/ and
#                    fetches nothing.
#
# `card` does not depend on `media`, so a card built without it is complete
# except for the data and names the file that is absent.
#
# `make media` fetches two files, both plain downloads from archive.org:
#
#   pak0.pak — the shareware episode, "Dimension of the Doomed". Freely
#   redistributable by id Software's own terms and a complete playable game
#   on its own. Fetched from the Internet Archive's dedicated shareware-PAK
#   item, which carries nothing but the PAK itself.
#
#   pak1.pak — the rest of registered Quake, the other three episodes.
#   Fetched from an Internet Archive item hosting the full retail data files.
#
# What arrives is checked against the MD5 each source publishes in its own
# item metadata, independently of this download, plus the SHA256 computed
# from the file this project fetched. Re-running re-verifies rather than
# re-downloading.
MEDIA_DIR = media

PAK0_PAK    = $(MEDIA_DIR)/pak0.pak
PAK0_URL    = https://archive.org/download/quake-shareware-pak/PAK0.PAK
PAK0_MD5    = 5906e5998fc3d896ddaf5e6a62e03abb
PAK0_SHA256 = 35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af

PAK1_PAK    = $(MEDIA_DIR)/pak1.pak
PAK1_URL    = https://archive.org/download/pak1_20260629/Quake/id1/pak1.pak
PAK1_MD5    = d76b3e5678f0b64ac74ce5e340e6a685
PAK1_SHA256 = 94e355836ec42bc464e4cbe794cfb7b5163c6efa1bcc575622bb36475bf1cf30

# sha256sum and md5sum on Linux, shasum and md5 on macOS. Whichever exists;
# if either is missing the target stops rather than accepting a download it
# cannot check.
SHA256SUM := $(firstword $(shell command -v sha256sum 2>/dev/null) \
                         $(shell command -v shasum 2>/dev/null))
MD5SUM    := $(firstword $(shell command -v md5sum 2>/dev/null) \
                         $(shell command -v md5 2>/dev/null))

# One argument: a friendly name, the destination path, the URL, the expected
# MD5 and the expected SHA256. Defined once and called twice, so pak0 and
# pak1 are fetched and verified identically rather than as two near-copies of
# the same shell.
define FETCH_AND_VERIFY
	@if [ -f "$(2)" ]; then \
		echo "  MEDIA $(2) already here — verifying"; \
	else \
		echo "  MEDIA fetching $(3)"; \
		curl -fL --retry 3 -o "$(2).part" "$(3)" || { \
			rm -f "$(2).part"; \
			echo "  MEDIA download failed"; exit 1; }; \
		mv "$(2).part" "$(2)"; \
	fi
	@got=`$(MD5SUM) -q "$(2)" 2>/dev/null || $(MD5SUM) "$(2)"`; \
	got=`echo "$$got" | awk '{print $$1}'`; \
	if [ "$$got" != "$(4)" ]; then \
		echo "  MEDIA MD5 MISMATCH for $(2)"; \
		echo "        expected $(4)"; \
		echo "        got      $$got"; \
		echo "        the file has been left in place for inspection, and is"; \
		echo "        NOT safe to put on a card."; \
		exit 1; \
	fi
	@got=`$(SHA256SUM) -a 256 "$(2)" 2>/dev/null || $(SHA256SUM) "$(2)"`; \
	got=`echo "$$got" | awk '{print $$1}'`; \
	if [ "$$got" != "$(5)" ]; then \
		echo "  MEDIA SHA256 MISMATCH for $(2)"; \
		echo "        expected $(5)"; \
		echo "        got      $$got"; \
		exit 1; \
	fi
	@echo "  MEDIA $(2) verified ($$(wc -c < $(2) | tr -d ' ') bytes)"
endef

media:
	@if [ -z "$(SHA256SUM)" ] || [ -z "$(MD5SUM)" ]; then \
		echo "  MEDIA no checksum tool on this machine (sha256sum/shasum and"; \
		echo "        md5sum/md5 are both needed) — refusing to download"; \
		echo "        something that cannot be verified."; \
		exit 1; \
	fi
	@mkdir -p $(MEDIA_DIR)
	$(call FETCH_AND_VERIFY,pak0.pak,$(PAK0_PAK),$(PAK0_URL),$(PAK0_MD5),$(PAK0_SHA256))
	$(call FETCH_AND_VERIFY,pak1.pak,$(PAK1_PAK),$(PAK1_URL),$(PAK1_MD5),$(PAK1_SHA256))
	@printf '%s\n' \
		"pak0.pak — the Quake shareware episode, \"Dimension of the Doomed\"" \
		"" \
		"Source:   $(PAK0_URL)" \
		"Item:     https://archive.org/details/quake-shareware-pak" \
		"Fetched:  `date -u '+%Y-%m-%d %H:%M:%S UTC'`" \
		"MD5:      $(PAK0_MD5)  (published in the item's own metadata," \
		"          independently of this download)" \
		"SHA256:   $(PAK0_SHA256)  (computed from this download)" \
		"" \
		"Licence: id Software's original 1996 shareware terms. The shareware" \
		"episode is freely redistributable, unmodified, for free," \
		"non-commercially, and is a complete playable game on its own." \
		"" \
		"---" \
		"" \
		"pak1.pak — the rest of registered Quake: episodes two through four" \
		"" \
		"Source:   $(PAK1_URL)" \
		"Item:     https://archive.org/details/pak1_20260629" \
		"Fetched:  `date -u '+%Y-%m-%d %H:%M:%S UTC'`" \
		"MD5:      $(PAK1_MD5)  (published in the item's own metadata," \
		"          independently of this download)" \
		"SHA256:   $(PAK1_SHA256)  (computed from this download)" \
		"" \
		"Quake is a trademark of id Software. These files are not ours, are" \
		"not redistributed by this repository, and are downloaded only by a" \
		"person running 'make media' on their own machine." \
		> $(MEDIA_DIR)/provenance.txt
	@echo "  MEDIA provenance written to $(MEDIA_DIR)/provenance.txt"

# The Pi 5 netboot bundle: the image the Pi 5 firmware looks for, plus the
# boot configuration it must be served alongside. Copy the contents into the
# TFTP root the board boots from (the Raspberry Pi firmware files themselves
# come from that root's existing installation, not from here).
NETBOOT_DIR = build/netboot-rpi5
netboot: rpi5
	@mkdir -p $(NETBOOT_DIR)
	@cp host/build/rpi5/$(IMAGE_rpi5) $(NETBOOT_DIR)/
	@cp host/config.txt host/cmdline.txt $(NETBOOT_DIR)/
	@echo "  STAGED $(NETBOOT_DIR)/"
	@ls -l $(NETBOOT_DIR)/

# The card, staged into a directory to copy onto media formatted elsewhere:
# the three kernels, the boot configuration, and whatever game data media/
# happens to hold.
#
# THIS TARGET NEVER DOWNLOADS ANYTHING. It copies what `make media` left and
# names what is absent, so a card built with no network is a real card that
# reports exactly what it is short of.
CARD_DIR  = build/sd-card
CARD_GAME = $(CARD_DIR)/tyrquake/id1

card: kernels
	@rm -rf $(CARD_DIR)
	@mkdir -p $(CARD_GAME)
	@cp host/build/rpi3/$(IMAGE_rpi3) $(CARD_DIR)/
	@cp host/build/rpi4/$(IMAGE_rpi4) $(CARD_DIR)/
	@cp host/build/rpi5/$(IMAGE_rpi5) $(CARD_DIR)/
	@cp host/config.txt host/cmdline.txt $(CARD_DIR)/
	@echo "  STAGED $(CARD_DIR)/"
	@for f in pak0.pak pak1.pak; do \
		if [ -f "$(MEDIA_DIR)/$$f" ]; then \
			cp "$(MEDIA_DIR)/$$f" $(CARD_GAME)/; \
			echo "  DATA   $$f"; \
		fi; \
	done
	@echo
	@if [ -f $(CARD_GAME)/pak0.pak ]; then :; else \
		echo "  ABSENT pak0.pak. The game cannot start without it. It is the"; \
		echo "         free shareware episode — 'make media' fetches it."; \
	fi
	@if [ -f $(CARD_GAME)/pak1.pak ]; then :; else \
		echo "  NOTE   pak1.pak absent — the shareware episode alone still"; \
		echo "         runs; this only completes the registered game."; \
	fi
	@echo "  NOTE   The Raspberry Pi firmware files are not staged here either."
	@echo "         See README.md."

clean-boards:
	@for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b clean-board; done
	rm -rf $(NETBOOT_DIR) $(CARD_DIR)

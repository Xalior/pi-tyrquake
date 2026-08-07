# pi-tyrquake

**Quake running directly on a Raspberry Pi with no operating system.** The
board powers on and the game is what boots: no Linux, no desktop, no
launcher, and nothing else running beside it.

It builds for the Raspberry Pi 3, Pi 4 and Pi 5, all three from one source
tree.

## What this is

[TyrQuake](https://disenchant.net/tyrquake/) is a conservative, well
maintained version of the original 1996 Quake engine. It keeps the game as id
Software wrote it and fixes what has broken over the years. This repository is
the thin layer that lets it run with nothing underneath: a
[Circle](https://github.com/rsta2/circle) kernel that brings the board up, and
[circle-libsdl2](https://github.com/Xalior/circle-libsdl2), an SDL2
implementation built on Circle's bare-metal drivers.

The game's own source is not copied or modified here. It is a submodule,
pinned at the upstream release tag v0.71, and the build reads it without ever
writing to it. Where the game needs something the SDL2 layer or the C library
does not provide, this repository supplies it in `host/` rather than changing
the game.

TyrQuake builds five different programs. This port builds one of them,
`tyr-quake`: the single-player NetQuake client with the **software renderer**.
Quake's software renderer draws every pixel with the processor and needs no
graphics hardware at all, which is what makes it the right one for a board
with no OpenGL. The two OpenGL programs need a graphics driver that does not
exist here, and the two QuakeWorld programs are for internet play and need a
network stack that does not either.

Three processor cores are given separate work:

- **Core 0** owns the hardware. Circle's world lives here — interrupts, USB,
  the SD card, sound — and no other core touches a device.
- **Core 1** runs the game and nothing else. This is where the whole software
  renderer runs, so this core is what decides the frame rate.
- **Core 2** puts finished frames on the screen. The game draws at 320x240 and
  never learns the display's size; the picture is scaled once, at the end,
  onto whatever the screen is really showing.

## State of this port

This is an early port. It builds and links completely, for all three boards,
and it has not yet been run on hardware. The list below is what the code does,
not what has been observed.

**Present:**

- Video: the full 320x240 paletted software rendering path, converted to
  32-bit once per frame and scaled to the display.
- Sound: circle-libsdl2 implements SDL's own audio API, including the older
  single-device calls TyrQuake's sound driver is written against.
- Keyboard and mouse: USB keyboards and mice through Circle's HID drivers.
  circle-libsdl2 implements SDL's relative mouse mode and reports movement as
  it happens, which is what mouse-look needs. That part of the library is
  newer than its own changelog and nothing in this port has confirmed it on
  hardware yet.
- Files: the PAK files, the configuration and the save games, read from and
  written to the SD card.

**Absent, and why:**

- **Multiplayer.** Quake's network play needs BSD sockets, and circle-libsdl2
  has no network stack behind it. The build selects upstream's own
  loopback-only network driver, which is the driver Quake has always used for
  a single-player game.
- **CD music.** Quake's soundtrack was audio tracks on the game CD. There is
  no optical drive on a Raspberry Pi, so the build selects upstream's own
  no-CD driver.
- **Quitting.** Choosing quit ends the program, and on a board with no
  operating system there is nothing to return to. The board stops. Turn it off
  and on again to play again.

## Game data, and `make media`

**This repository contains no game data, and cannot.** Quake's levels,
graphics and sounds live in PAK files that are not part of the engine and are
not this project's to distribute. Building the images does not download one,
and neither does writing a card.

Two directories, and the difference between them matters:

| | |
|---|---|
| `media/` | Where game data lives on your machine. `make media` downloads into it; you can also copy your own files into it by hand. It is never committed and never shipped. |
| `build/sd-card/` | What `make card` stages. It **copies from `media/`** and fetches nothing. |

`make card` works whether or not `media/` has anything in it. A card built
with no data is a real card — it just says plainly which files are missing.

You need at least `id1/pak0.pak`.

| File | What it is |
|---|---|
| `id1/pak0.pak` | The Quake shareware episode, *Dimension of the Doomed*. Freely redistributable by id Software's own terms, and a complete playable game on its own. |
| `id1/pak1.pak` | The rest of registered Quake — the other three episodes. |

### `make media` — the two things that can be downloaded

```sh
make media
```

It downloads both files, with `curl`, from public Internet Archive items:

- **`pak0.pak`** — the shareware episode, distributed by id Software for free
  copying. It travels inside the original shareware release, which the
  Internet Archive holds as a standalone PAK.
- **`pak1.pak`** — the rest of registered Quake, from an Internet Archive item
  hosting the retail data files.

What arrives is checked against the MD5 each source publishes in its own item
metadata, independently of this download, and against the SHA256 computed from
the file this project fetched. If either does not match, the target stops and
says so rather than handing you a file to put on a card. A `provenance.txt` is
written beside them recording the URLs, the date, the licence and the hashes.
Running it again re-verifies what is already there instead of downloading it
a second time.

Read this section before you run it. It is your machine and your
responsibility.

## Building

You need a Linux or macOS machine, GNU make, and the Arm GNU toolchain for
`aarch64-none-elf` (release 15.2.Rel1). Put its `bin` directory on your
`PATH`, or unpack it into `toolchains/` in this repository.

```sh
git clone --recursive https://github.com/Xalior/pi-tyrquake.git
cd pi-tyrquake
make deps       # long: builds newlib and libc++ from source, once per board
make kernels    # the three board images
make verify     # confirms each image exists and is not empty
```

`make deps` is the slow step, and it is slow once. It builds a complete C and
C++ world for each board, because each board's world is compiled for its own
processor.

Part of that world is libc++, whose sources are fetched from a git tag that
carries the bare-metal patches. That tag is hosted on Codeberg, which is small
and volunteer run. One copy is enough for every board and for every project on
your machine, so tell the build where to keep it and it is fetched once:

```sh
make deps CIRCLE_LLVM=/path/to/circle-llvm
```

The default puts that checkout beside this repository, which is the right
answer for a plain clone or a continuous-integration runner and needs no
setting at all.

The images land in `host/build/<board>/`:

| Board | Image |
|---|---|
| Pi 3 | `host/build/rpi3/kernel8.img` |
| Pi 4 | `host/build/rpi4/kernel8-rpi4.img` |
| Pi 5 | `host/build/rpi5/kernel_2712.img` |

Building one board on its own is `make rpi5`, and its dependencies alone are
`make deps-rpi5`, which is what a machine without room for three worlds
wants.

## Putting it on a card

```sh
make card
```

That stages the card into `build/sd-card/` for you to copy onto FAT32 media:
the three kernel images under the names each board's firmware looks for, the
boot configuration, and whatever game data `media/` holds, copied into
`tyrquake/id1/`. It never downloads anything itself — see *Game data, and
`make media`* above — and it names on the console whatever it does not find.

One thing is not staged and has to be added by hand: **the Raspberry Pi
firmware files** — `bootcode.bin`, `start*.elf`, `fixup*.dat` and, for the
Pi 4, `armstub8-rpi4.bin`. Take them from a Raspberry Pi OS card or from the
[firmware repository](https://github.com/raspberrypi/firmware).

Everything this game reads or writes stays inside `tyrquake/` on the card. One
card can carry several games, and a game that wrote its `config.cfg` into the
card's root would overwrite another game's.

### The thermal settings in `cmdline.txt`

One card boots any of the three boards, so all three read the same
`cmdline.txt`. It carries `socmaxtemp=70`, the temperature in degrees Celsius
at which the processor is slowed down to cool itself.

If your board has a fan, add `gpiofanpin=` and the GPIO pin it is wired to —
`gpiofanpin=45` is a Raspberry Pi 5 Case Fan or Active Cooler. Naming a fan
pin changes what happens at that temperature: the fan is switched on and the
processor is left at full speed, instead of being slowed down. That is what
this game wants more than most, because every pixel it draws is processor
work and a slowed processor drops frames.

## License

The code in this repository — the kernel layer in `host/` and the build — is
released under the GNU Lesser General Public License, version 3. See
[LICENSE](LICENSE).

The submodules are other people's work and carry their own terms, and both
matter before you distribute anything you build here:

- **TyrQuake**, and the Quake engine it is derived from, are released under
  the GNU General Public License, version 2 or later.
- **Circle** is released under the GNU General Public License, version 3.

Building a kernel image here combines all of them, and the result is covered
by the GNU General Public License, version 3. Doing that for yourself is
straightforward; redistributing the result means satisfying every one of those
terms at once, including supplying complete source.

Quake is a trademark of id Software LLC. This project is not affiliated with
id Software or ZeniMax.

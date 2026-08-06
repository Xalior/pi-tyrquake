//
// circle_stubs.cpp — the SDL2 and C library entry points TyrQuake reaches
// that circle-libsdl2 and newlib do not provide.
//
// TyrQuake asks very little of SDL. It draws every pixel itself, into an
// 8-bit buffer of its own, and hands SDL one streaming texture a frame — so
// none of SDL's surface, blitting or scaling machinery is involved, and the
// library covers the window, the renderer, the texture, the events and the
// audio device on its own. What is left is a short list, and it divides in
// two.
//
// FIRST, the pixel-format object. SDL lets a caller hold a description of a
// pixel layout, ask a colour to be packed into it, and free it again.
// TyrQuake uses that to build its 256-entry palette lookup table once per
// palette change. circle-libsdl2 has exactly one pixel layout — 32-bit
// ARGB — and so has never needed the object; this file provides it, and
// takes the masks from the library's own format table rather than writing a
// second copy of them.
//
// SECOND, the older audio calls. SDL2 carries two audio interfaces: the
// device one, which circle-libsdl2 implements, and the single-device one
// inherited from SDL 1.2, which is written in terms of it. TyrQuake's sound
// driver uses the older one. The three functions here are that translation
// and nothing more.
//
// The C library additions at the end are the same idea: calls TyrQuake's
// operating-system layer makes that newlib on this board does not carry.
//
// These are seams, not permanent furniture. When the shim implements one of
// these for real, the way to adopt it is to DELETE the stub here: the
// archive is linked whole, so a leftover stub becomes a duplicate-symbol
// error at link time rather than a silent winner over the real thing.
//
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <cerrno>
#include <ctime>
#include <unistd.h>

#include <SDL2/SDL.h>

extern "C" {

// ---------------------------------------------------------------------------
// Pixel formats
// ---------------------------------------------------------------------------
//
// SDL_AllocFormat turns a format enumeration into an object carrying the
// masks and shifts needed to pack a colour. The library already knows the
// masks for every format it supports — SDL_PixelFormatEnumToMasks is its
// answer — so this asks it rather than keeping a second table that could
// disagree with the first.
//
// The shift and loss values are derived from the masks: the shift is how far
// up the channel sits, and the loss is how many of a byte's eight bits the
// channel cannot hold. That derivation is what SDL's own allocator does, and
// it means a format the library adds later works here with no change.

static Uint8 MaskShift(Uint32 mask)
{
    if (mask == 0)
        return 0;
    Uint8 shift = 0;
    while ((mask & 1) == 0)
    {
        mask >>= 1;
        shift++;
    }
    return shift;
}

static Uint8 MaskLoss(Uint32 mask)
{
    if (mask == 0)
        return 8;
    mask >>= MaskShift(mask);
    Uint8 bits = 0;
    while (mask & 1)
    {
        mask >>= 1;
        bits++;
    }
    return (Uint8)(8 - bits);
}

SDL_PixelFormat *SDL_AllocFormat(Uint32 pixel_format)
{
    int bpp = 0;
    Uint32 rmask = 0, gmask = 0, bmask = 0, amask = 0;

    if (SDL_PixelFormatEnumToMasks(pixel_format, &bpp,
                                   &rmask, &gmask, &bmask, &amask) != SDL_TRUE)
    {
        SDL_SetError("pixel format 0x%08x is not implemented", pixel_format);
        return nullptr;
    }

    SDL_PixelFormat *fmt = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    if (fmt == nullptr)
    {
        SDL_SetError("out of memory allocating pixel format");
        return nullptr;
    }

    fmt->format        = pixel_format;
    fmt->BitsPerPixel  = (Uint8)bpp;
    fmt->BytesPerPixel = (Uint8)((bpp + 7) / 8);
    fmt->Rmask         = rmask;
    fmt->Gmask         = gmask;
    fmt->Bmask         = bmask;
    fmt->Amask         = amask;
    fmt->Rshift        = MaskShift(rmask);
    fmt->Gshift        = MaskShift(gmask);
    fmt->Bshift        = MaskShift(bmask);
    fmt->Ashift        = MaskShift(amask);
    fmt->Rloss         = MaskLoss(rmask);
    fmt->Gloss         = MaskLoss(gmask);
    fmt->Bloss         = MaskLoss(bmask);
    fmt->Aloss         = MaskLoss(amask);
    fmt->refcount      = 1;
    return fmt;
}

void SDL_FreeFormat(SDL_PixelFormat *format)
{
    if (format == nullptr)
        return;
    if (--format->refcount > 0)
        return;
    free(format);
}

// Pack a colour the way the format describes. Every format the library
// offers is direct colour, so there is no palette to search: the channels
// are shifted into place and the alpha is set to fully opaque, which is what
// a caller asking for an opaque colour means.
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
    if (format == nullptr)
        return 0;
    return ((Uint32)(r >> format->Rloss) << format->Rshift)
         | ((Uint32)(g >> format->Gloss) << format->Gshift)
         | ((Uint32)(b >> format->Bloss) << format->Bshift)
         | format->Amask;
}

Uint32 SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b,
                   Uint8 a)
{
    if (format == nullptr)
        return 0;
    return ((Uint32)(r >> format->Rloss) << format->Rshift)
         | ((Uint32)(g >> format->Gloss) << format->Gshift)
         | ((Uint32)(b >> format->Bloss) << format->Bshift)
         | ((Uint32)(a >> format->Aloss) << format->Ashift);
}

// Only ever printed, and only when something has already gone wrong: the
// game names the format in the error it stops on. The two the library can
// produce are named; anything else is reported as its number, which is more
// use than a wrong name.
const char *SDL_GetPixelFormatName(Uint32 format)
{
    switch (format)
    {
    case SDL_PIXELFORMAT_ARGB8888: return "SDL_PIXELFORMAT_ARGB8888";
    case SDL_PIXELFORMAT_RGB888:   return "SDL_PIXELFORMAT_RGB888";
    default:
        break;
    }
    static char name[24];
    snprintf(name, sizeof(name), "0x%08x", (unsigned)format);
    return name;
}

// ---------------------------------------------------------------------------
// The single-device audio interface
// ---------------------------------------------------------------------------
//
// SDL 1.2 had one audio device and named no identifier. SDL2 keeps those
// calls as a thin layer over the device interface, opening the first device
// and remembering it. circle-libsdl2 has exactly one sound device and
// already refuses a second, so the layer here is a forward and a change of
// return convention: the device call answers with an identifier, zero
// meaning failure, and the older call answers with zero for success.

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    return SDL_OpenAudioDevice(nullptr, 0, desired, obtained, 0) != 0 ? 0 : -1;
}

void SDL_PauseAudio(int pause_on)
{
    SDL_PauseAudioDevice(1, pause_on);
}

void SDL_CloseAudio(void)
{
    SDL_CloseAudioDevice(1);
}

// ---------------------------------------------------------------------------
// C library calls newlib does not carry on this board
// ---------------------------------------------------------------------------

// The frame loop's yield between ticks. SDL's own delay is the right thing
// underneath: on the application core it releases the core to the shim
// rather than spinning on it.
int usleep(useconds_t usec)
{
    SDL_Delay((Uint32)((usec + 999) / 1000));
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (req == nullptr || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L)
    {
        errno = EINVAL;
        return -1;
    }
    SDL_Delay((Uint32)(req->tv_sec * 1000 + (req->tv_nsec + 999999L) / 1000000L));
    // Nothing here can interrupt a sleep, so the whole interval always
    // elapses and no remainder is ever left.
    if (rem != nullptr)
    {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

} // extern "C"

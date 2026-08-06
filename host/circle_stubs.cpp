//
// circle_stubs.cpp — the C library entry points TyrQuake reaches that
// newlib on this board does not provide.
//
// TyrQuake asks very little of SDL. It draws every pixel itself, into an
// 8-bit buffer of its own, and hands SDL one streaming texture a frame — so
// none of SDL's surface, blitting or scaling machinery is involved, and the
// library covers the window, the renderer, the texture, the events and the
// audio device on its own. circle-libsdl2 now provides every SDL entry
// point TyrQuake calls, so what remains here is entirely non-SDL: calls
// TyrQuake's operating-system layer makes that newlib on this board does
// not carry.
//
// These are seams, not permanent furniture. When the shim implements one of
// these for real, the way to adopt it is to DELETE the stub here: the
// archive is linked whole, so a leftover stub becomes a duplicate-symbol
// error at link time rather than a silent winner over the real thing.
//
#include <cerrno>
#include <ctime>
#include <unistd.h>

#include <SDL2/SDL.h>

extern "C" {

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

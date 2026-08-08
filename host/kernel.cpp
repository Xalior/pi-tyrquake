//
// kernel.cpp — brings the board up, then hands control to TyrQuake.
//
// TyrQuake is an ordinary command-line program: it expects a working
// standard library, a filesystem holding its PAK files, and an SDL2
// implementation. This file supplies the first two and starts the third,
// then calls the game's entry point with a fixed argument list.
//
// EVERYTHING THIS GAME TOUCHES LIVES IN ONE DIRECTORY ON THE CARD,
// RAPI_GAME_DIR, which the host Makefile sets. One card carries several
// games, and two of them writing a `config.cfg` into the card's root would
// each silently overwrite the other's.
//
// Two things point at it, and between them they cover every path the game
// can reach. QBASEDIR, the build knob upstream already has, is set to it, so
// the game looks for `id1/` — its PAK files, its configuration and its save
// games — inside it. And this kernel makes it the working directory before
// the game starts, so anything opened by a relative name lands there too
// rather than in the root. Upstream is pointed, never patched.
//
// This kernel also decides the core layout (see kernel.h for the roles) and
// hands one core to the shim's presentation worker. The library never starts
// a core; electing one is the host's job, and this is where it happens. The
// game itself knows none of it: it calls plain SDL, and its file access
// reaches the marshalled I/O service through the syscall layer in
// circle_syscalls.cpp.
//
#include "kernel.h"
#include "defaults.h"
#include "defaultsblock.h"
#include <circle/startup.h>
#include <circle/machineinfo.h>
#include <SDL2/SDL_circle.h>
#include <SDL2/SDL_error.h>
#include <unistd.h>
#include <atomic>

// TyrQuake's entry point. It is main() in the upstream source; the build
// renames it for that one translation unit, because main() here belongs to
// the Circle kernel. Declared extern "C" because the upstream source is C,
// and the signature must match upstream's exactly.
extern "C" int tyrquake_main(int argc, char **argv);

void CGlueStdioInit(CConsole &rConsole);

static const char From[] = "tyrquake";

// ---------------------------------------------------------------------------
// The picture the game draws.
//
// 320x240 is Quake's classic software resolution, and it is a decision about
// work rather than about taste: every pixel of a Quake frame is rasterized by
// the processor, so the cost of a frame is the pixel count. The library
// carries the finished picture to whatever the screen is really showing, in
// one pass on the presentation core, so this number is not the screen's.
//
// The same pair is spelled twice, as numbers for the library and as text for
// the game's own command line, and both come from one definition so they
// cannot drift apart. They must agree: the game asks SDL for a fullscreen
// mode of exactly this size, and the library offers exactly one mode.
// ---------------------------------------------------------------------------
#define RAPI_VID_WIDTH   320
#define RAPI_VID_HEIGHT  240

#define RAPI_STRINGIFY_(x) #x
#define RAPI_STRINGIFY(x)  RAPI_STRINGIFY_(x)

// The game's command line. These are the BAKED arguments; anything written
// into the image's defaults block is appended to them before the game runs,
// so a boot can add to this without the card or the build changing.
//
// -fullscreen with a size names the one video mode the library offers.
// Without it TyrQuake starts in its built-in 640x480 windowed mode, which is
// not a mode this display has.
//
// -mem is the heap the game allocates in one block at startup. Upstream's
// default is 256MB, sized for a desktop; 64MB is far more than any Quake
// level needs at this resolution, and it keeps the demand the same on all
// three boards, including the Pi 3.
static const char *TyrQuakeArgv[] = {
    RAPI_GAME_DIR "/tyrquake",
    "-fullscreen",
    "-width",  RAPI_STRINGIFY(RAPI_VID_WIDTH),
    "-height", RAPI_STRINGIFY(RAPI_VID_HEIGHT),
    "-mem",    "64",
};

// The final list: the baked arguments, plus whatever the block carries.
// Sized for the block's worst case — every byte of its capacity a
// single-character argument — on top of the baked ones, plus the
// terminating null.
static const char *s_FinalArgv[sizeof(TyrQuakeArgv) / sizeof(TyrQuakeArgv[0])
                               + DEFAULTS_BUFFER_BYTES / 2 + 1];
static int s_FinalArgc = 0;

// ---------------------------------------------------------------------------
// The gate between core 0 and the application core.
//
// The cores are started at the end of Initialize, because that is where a
// Circle world is finished. But the application must not begin until the
// shim's split is armed — until then its platform calls would run on the
// wrong core with no mailbox to carry them. So the application core waits
// here, and core 0 opens the gate once SDL2Circle_SplitInit has returned.
//
// The return travels back the same way. Core 0 cannot join a core, so the
// application core publishes the result and core 0 watches for it while
// yielding to the scheduler — which is what keeps the servo, the watchdog
// and every device alive for as long as the game runs.
// ---------------------------------------------------------------------------

static std::atomic<int> s_AppGate{0};      // core 0 -> application core
static std::atomic<int> s_AppDone{0};      // application core -> core 0
static int s_AppResult = -1;

static inline void PublishToOtherCores(void)
{
    asm volatile("dsb ish; sev" ::: "memory");
}

static void ParkCore(void)
{
    for (;;)
        asm volatile("wfe" ::: "memory");
}

void CSplitCores::Run(unsigned nCore)
{
    // Before this core runs anything of ours: every core here may reach code
    // that throws, and a throw reads this register first.
    SDL2Circle_ArmCoreRuntime();

    switch (nCore)
    {
    case 1:
        // The application core. Wait for the gate, run the game, publish
        // what it returned, then go quiet — this core has no other purpose
        // and must not fall through into anything.
        while (!s_AppGate.load(std::memory_order_acquire))
            asm volatile("wfe" ::: "memory");

        s_AppResult = tyrquake_main(s_FinalArgc, const_cast<char **>(s_FinalArgv));

        s_AppDone.store(1, std::memory_order_release);
        PublishToOtherCores();
        ParkCore();
        break;

    case 2:
        // The elected presentation core. Never returns.
        SDL2Circle_SplitPresentCore();
        break;

    default:
        ParkCore();
        break;
    }
}

CKernel::CKernel(void)
    // Serial device 0 is the GPIO14/15 header UART on every board. Named
    // explicitly because Circle's RASPPI >= 5 default (SERIAL_DEVICE_DEFAULT
    // = 10) is the Pi 5's dedicated debug connector, not the header.
    : m_Serial(0, FALSE, 0),
      m_Timer(&m_Interrupt),
      m_Logger(m_Options.GetLogLevel(), &m_Timer),
      m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED),
      m_Console(&m_Serial, &m_Serial),    // stdio over the UART
      m_USB(&m_Interrupt, &m_Timer, TRUE /* plug-and-play */)
{
    m_ActLED.Blink(3);
}

// Build-timestamp epoch (seconds since 1970-01-01 UTC) from __DATE__/__TIME__.
// Monotonic across releases, always a plausible "now".
static unsigned BuildEpoch(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;   // "Mmm dd yyyy"
    const char *t = __TIME__;   // "hh:mm:ss"

    int mon = 1;
    for (int i = 0; i < 12; i++)
        if (d[0] == months[i*3] && d[1] == months[i*3+1] && d[2] == months[i*3+2])
            { mon = i + 1; break; }
    int day  = (d[4] == ' ' ? 0 : d[4] - '0') * 10 + (d[5] - '0');
    int year = (d[7]-'0')*1000 + (d[8]-'0')*100 + (d[9]-'0')*10 + (d[10]-'0');
    int hh = (t[0]-'0')*10 + (t[1]-'0');
    int mm = (t[3]-'0')*10 + (t[4]-'0');
    int ss = (t[6]-'0')*10 + (t[7]-'0');

    // days since 1970-01-01 (civil-to-days, treated as UTC)
    int y = year - (mon <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe*365 + yoe/4 - yoe/100 + doy;
    long days = (long)era*146097 + (long)doe - 719468;
    return (unsigned)(days * 86400L + hh*3600 + mm*60 + ss);
}

boolean CKernel::Initialize(void)
{
    boolean bOK = TRUE;
    if (bOK) bOK = m_Serial.Initialize(115200);
    if (bOK) bOK = m_Logger.Initialize(&m_Serial);
    if (bOK) bOK = m_Interrupt.Initialize();
    if (bOK) bOK = m_Timer.Initialize();
    // No battery RTC on a Pi: seed the wall clock with the build time —
    // like a device whose clock was set once at the factory — so time()
    // is plausible. The game stamps its save games with it.
    if (bOK) m_Timer.SetTime(BuildEpoch(), FALSE /* universal */);
    if (bOK) bOK = m_EMMC.Initialize();

    // Slide the disk counter in between the card and everything that uses it,
    // after the card has registered its name and before the first mount reads
    // a sector. FatFs finds its device by name and holds no pointer to the
    // card, so taking the name over is the whole of the interposition — no
    // caller changes and nothing above this line knows. It counts and passes
    // through; it caches nothing. Not fatal if it fails: the name still
    // resolves to the card itself and the game runs uninstrumented.
    if (bOK && !m_DiskStats.Install())
        m_Logger.Write(From, LogWarning,
                       "disk instrumentation did not install — the card is "
                       "unwrapped and no disk figures will be reported");

    if (bOK) bOK = (f_mount(&m_FileSystem, "SD:", 1) == FR_OK);
    if (bOK) bOK = m_Console.Initialize();
    if (bOK) CGlueStdioInit(m_Console);

    // USB, here and not in the game's SDL_Init.
    //
    // Enumeration is slow, interrupt-driven and free to take as long as it
    // needs — right here, on core 0, with the whole machine to itself and
    // nothing yet depending on it answering. That is exactly what it is NOT
    // once the split is armed: from then on core 0's servo is the only thing
    // answering the other cores, and a long call inside it stops everything.
    //
    // Not fatal. A board with no working USB still runs the game, just with
    // no keyboard and no pad, and that is worth saying rather than dying for.
    if (bOK && !m_USB.Initialize())
        m_Logger.Write(From, LogWarning,
                       "USB did not come up — the game will run without a "
                       "keyboard or a game pad");

    // Core 0 runs application and library code like any other core, so it
    // arms itself too — before the secondary cores start, and before the
    // first thing that can throw.
    if (bOK) SDL2Circle_ArmCoreRuntime();

    // Start the secondary cores last: the world they are about to work in
    // has to be complete first — the card mounted, stdio wired — because
    // core 0 will be busy serving them from the moment they run. They park
    // in CSplitCores::Run until Run() below arms the split and opens the
    // gate.
    if (bOK) bOK = m_Cores.Initialize();
    return bOK;
}

TShutdownMode CKernel::Run(void)
{
    m_Logger.Write(From, LogNotice, "starting TyrQuake");

    // Geometry evidence belongs on serial: what boot config handed us, read
    // next to the shim's framebuffer-grant line when the window is created.
    // This is the PHYSICAL request only — the mode asked of the firmware.
    // The card asks for no size, so this prints 0x0 and the panel keeps its
    // own mode. It never sets what the game is given: that is the
    // declaration below.
    m_Logger.Write(From, LogNotice, "boot config geometry: %ux%u",
                   m_Options.GetWidth(), m_Options.GetHeight());

    // The VIRTUAL display the game is given, declared before anything asks
    // the library about the display. Every SDL answer TyrQuake gets — the
    // one enumerated mode, the desktop mode, the window, the renderer's
    // output size — is this, whatever the panel is really scanning, and the
    // library carries each frame from here to there in one pass on the
    // presentation core.
    //
    // The library has no default and no fallback: without this it refuses to
    // start.
    if (SDL2Circle_DeclareVirtualDevice(32, RAPI_VID_WIDTH, RAPI_VID_HEIGHT) != 0)
    {
        m_Logger.Write(From, LogError, "virtual display %dx%d refused: %s",
                       RAPI_VID_WIDTH, RAPI_VID_HEIGHT, SDL_GetError());
        return ShutdownHalt;
    }
    m_Logger.Write(From, LogNotice, "virtual display declared: %dx%d at 32bpp",
                   RAPI_VID_WIDTH, RAPI_VID_HEIGHT);

    // Render throughput lives and dies by the ARM and core clocks. The shim
    // owns the class that manages them, so the readings come from the shim;
    // this kernel never makes a CCPUThrottle of its own, because Circle
    // allows exactly one and a second stops the board. Above the socmaxtemp
    // limit in the card's cmdline.txt the clock is pulled back to idle — or,
    // where that file also names a fan pin with gpiofanpin=, the fan is
    // switched on instead and the clock is left alone.
    m_Logger.Write(From, LogNotice,
                   "SoC: %uC, arm %u MHz, core %u MHz, socmaxtemp %uC",
                   SDL2Circle_SoCTemperature(),
                   SDL2Circle_CPUClockRate() / 1000000,
                   CMachineInfo::Get()->GetClockRate(CLOCK_ID_CORE) / 1000000,
                   CKernelOptions::Get()->GetSoCMaxTemp());

    // Build the game's argument list before anything else looks at it: the
    // baked arguments, plus whatever a pre-boot writer stamped into the
    // image. Kernel switches are taken out here and never reach the game.
    s_FinalArgc = DefaultsBuildArgv(TyrQuakeArgv,
                                    sizeof(TyrQuakeArgv) / sizeof(TyrQuakeArgv[0]),
                                    s_FinalArgv,
                                    sizeof(s_FinalArgv) / sizeof(s_FinalArgv[0]));

    // Move into this game's own directory before the game runs, so every
    // relative path it opens lands there and never in the card's root. One
    // card carries several games, and two of them writing a `config.cfg`
    // into the root would each silently overwrite the other's.
    //
    // Done here, on core 0, before the application core is released: the
    // working directory is one global, so setting it here covers the
    // application core too.
    // A failure is worth saying out loud but is not fatal — the game will
    // still find its files through the absolute base directory it was built
    // with.
    if (chdir(RAPI_GAME_DIR) != 0)
        m_Logger.Write(From, LogWarning,
                       "could not enter " RAPI_GAME_DIR
                       " — relative paths will resolve at the card root");

    // Serial key injection, if the block asked for it.
    if (rapi_debug_uart)
    {
        m_Logger.Write(From, LogNotice,
                       "serial key injection armed (--rapi-debug-uart)");
    }

    // Performance reports — one serial line every N seconds, frame rate then
    // the cycle split — come from the library. Nothing to wire here: the
    // defaults block's `--rapi-perf=N` was consumed above, which is where
    // SDL2Circle_SetPerfInterval gets called (see defaults.cpp).

    int res;
    m_Logger.Write(From, LogNotice,
                   "core split: hardware core 0, application core 1, presentation core 2");

    // Arm the split before the application's first instruction: the
    // servo and watchdog on core 0, and the mailboxes every marshalled
    // call rides. Then open the gate.
    SDL2Circle_SplitInit();
    s_AppGate.store(1, std::memory_order_release);
    PublishToOtherCores();

    // Core 0's idle loop for the whole run. Yielding is not politeness
    // here: the servo task is what answers the application core, feeds
    // the sound device and pumps USB, and it only runs when this loop
    // gives it the core.
    //
    // The disk report rides here too. It is printed from this loop and
    // nowhere else, deliberately: writing it from inside a read would put
    // serial output in the middle of a call the application core is waiting
    // on. Poll() costs one clock read until its five seconds are up.
    while (!s_AppDone.load(std::memory_order_acquire))
    {
        m_DiskStats.Poll();
        m_Scheduler.Yield();
    }
    res = s_AppResult;

    // One last report before parking, so a run that ends quickly still says
    // what it did.
    m_DiskStats.Report();

    // Park instead of rebooting. A reboot stops the clocks with the UART
    // FIFO still draining, so the exit line reaches the bench truncated —
    // and it destroys the machine state worth inspecting. The board sits
    // here until power-cycled.
    m_Logger.Write(From, LogNotice, "TyrQuake exited with %d — parked", res);
    for (;;)
    {
        m_Timer.MsDelay(1000);
    }
}

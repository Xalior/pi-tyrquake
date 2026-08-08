//
// diskstats.h — a block device that sits in front of the SD card and counts
// what goes past it.
//
// IT CACHES NOTHING AND CHANGES NOTHING. Every read and every write is handed
// straight to the real device and its result returned untouched, in the same
// order, with the same bytes. The only thing this layer adds is arithmetic on
// the side. That is the whole constraint it upholds: an image built with it
// installed must behave exactly like an image built without it, so the
// numbers it reports describe real disk traffic rather than traffic this
// layer shaped.
//
// It exists to answer a question that has to be settled before a read-ahead
// cache can be designed: what does the disk traffic actually look like? How
// big are the requests, how often does one carry on where the last one
// finished, how often is the same range asked for twice, and how long does
// the card really take? Guessing at those produces a cache sized for a
// program nobody is running.
//
// HOW IT GETS IN THE WAY. Circle's FatFs glue does not hold a pointer to the
// SD card. It looks the card up by name through CDeviceNameService — "emmc1"
// for the internal slot — and then calls Seek and Read on whatever it was
// handed. So a CDevice registered under that name is used by everything above
// it with nothing above it changed: FatFs, newlib's fopen and fread, and any
// library doing its own file access all arrive here without knowing it. The
// takeover is Install(), which looks the real device up, unregisters the name
// and registers this object in its place. The real device object is untouched
// and is not destroyed; only the name now resolves somewhere else.
//
// ONE THING DOES NOT PASS THROUGH. A caller that asks to be told when the
// device disappears — CDevice::RegisterRemovedHandler — registers with this
// object, and CDevice keeps that list privately with no way to fire it from a
// subclass, so the notification cannot be relayed from the real device. What
// this class does instead is give up the name when a removal it was asked for
// succeeds, so the layer above stops finding a device at all rather than
// finding one that fails every read. On an internal SD card the question
// never comes up: it cannot be removed and Circle's EMMC driver refuses the
// request. A host putting this in front of a device that really can be
// unplugged should settle that before relying on it.
//
// WHERE IT MAY RUN. Circle's devices belong to core 0, so every call that
// reaches this class arrives on core 0 already — that is a property of the
// host, not of this class. It therefore logs with CLogger directly, which is
// legal only because of that. If a host ever routes disk access from another
// core, this class must be revisited before anything else is.
//
// PORTABILITY. Circle and nothing else: no SDL, no project types, one header
// and one source file. It allocates nothing at any point — every counter and
// every buffer is a fixed-size member — so it is safe to install before the
// heap matters and cheap enough to leave in front of every disk access.
//
#ifndef _diskstats_h
#define _diskstats_h

#include <circle/device.h>
#include <circle/types.h>

// Requests are counted in sectors, and this is the size of one. Every layer
// in this stack — the SD card, FatFs and Circle's own glue — uses 512 bytes
// and none of them offers a way to ask.
#define DISKSTATS_SECTOR_SIZE       512

// How often a report reaches the log, in seconds.
#define DISKSTATS_REPORT_SECONDS    5

// Request sizes and seek distances are each counted into a small set of
// power-of-two buckets rather than kept individually.
#define DISKSTATS_SIZE_BUCKETS      8
#define DISKSTATS_JUMP_BUCKETS      6

// How many recently requested ranges are remembered, for the question "would
// a cache have had this already?". Only the range is kept — a start sector
// and a length — never the data that was read into it.
#define DISKSTATS_WINDOW_ENTRIES    64

class CDiskStatsDevice : public CDevice
{
public:
	/// \param pDeviceName Name in the device name service to take over.
	///        "emmc1" is the internal SD slot, which is what FatFs mounts.
	CDiskStatsDevice (const char *pDeviceName = "emmc1");
	~CDiskStatsDevice (void);

	/// \brief Take the name over from the real device.
	/// Call after the real device has been initialised — it is not in the
	/// name service before that — and before anything mounts or opens it.
	/// \return TRUE if the name was found and now resolves to this object.
	boolean Install (void);

	/// \brief Emit a report if the reporting interval has elapsed.
	/// Costs one clock read when it is not yet time, so it can be called as
	/// often as the host likes: an idle loop is the natural home. It must be
	/// called from somewhere allowed to write to the log — core 0, and not
	/// from inside a call another core is waiting on.
	void Poll (void);

	/// \brief Emit a report now, whether or not the interval has elapsed.
	void Report (void);

	// --- CDevice: everything below is pass-through ---
	int Read (void *pBuffer, size_t nCount) override;
	int Write (const void *pBuffer, size_t nCount) override;
	u64 Seek (u64 ullOffset) override;
	u64 GetSize (void) const override;
	int IOCtl (unsigned long ulCmd, void *pData) override;
	boolean RemoveDevice (void) override;

private:
	// One direction's worth of counters. Reads and writes are counted the
	// same way, so they share a shape.
	struct TDirStats
	{
		u64 nRequests;
		u64 nSectors;
		u64 nFailures;
		u64 nTotalMicros;
		unsigned nMinMicros;
		unsigned nMaxMicros;
		u64 nSizeBucket[DISKSTATS_SIZE_BUCKETS];
	};

	// A range that was asked for once. Not its contents.
	struct TRange
	{
		u64 nStartSector;
		u32 nSectors;
	};

	void ResetDir (TDirStats *pDir);
	void RecordTransfer (TDirStats *pDir, u64 nStartSector, u32 nSectors,
			     unsigned nMicros, boolean bOK);
	void RecordSequence (u64 nStartSector, u32 nSectors);
	void RecordWindow (u64 nStartSector, u32 nSectors);
	void ReportLegend (void);

	static unsigned SizeBucket (u32 nSectors);
	static unsigned JumpBucket (u64 nDistanceSectors);

	const char *m_pDeviceName;
	CDevice *m_pDevice;		// the real card, or 0 before Install()

	// Where the next Read or Write will land. Mirrors the real device's own
	// position: set by Seek, advanced by a successful transfer.
	u64 m_ullOffset;

	TDirStats m_Read;
	TDirStats m_Write;

	// Sequentiality. m_nNextExpected is the sector just past the end of the
	// last read, which is what a read-ahead would have fetched.
	u64 m_nNextExpected;
	boolean m_bHavePrevious;
	u64 m_nSequential;		// reads that started exactly there
	u64 m_nJumpForward;
	u64 m_nJumpBackward;
	u64 m_nJumpBucket[DISKSTATS_JUMP_BUCKETS];

	// The cache that is not there: the last few ranges asked for, and how
	// often a read could have been answered from them.
	TRange m_Window[DISKSTATS_WINDOW_ENTRIES];
	unsigned m_nWindowNext;
	unsigned m_nWindowUsed;
	u64 m_nWindowSame;		// the identical range, asked for again
	u64 m_nWindowInside;		// fully covered by one remembered range
	u64 m_nWindowEdge;		// partly covered
	u64 m_nWindowMiss;

	// Reporting. The wall clock is the 64-bit one: the 32-bit microsecond
	// counter wraps after about 71 minutes, which is well inside a session.
	// Individual transfers are timed with the 32-bit counter, where the
	// wrap cannot matter.
	u64 m_ullLastReportTicks;
	u64 m_ullFirstTicks;
	boolean m_bStarted;
	boolean m_bLegendPrinted;
	u64 m_nPrevReadRequests;
	u64 m_nPrevReadSectors;
	u64 m_nPrevWriteRequests;
	u64 m_nPrevWriteSectors;
};

#endif

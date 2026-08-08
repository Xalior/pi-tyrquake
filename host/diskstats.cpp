//
// diskstats.cpp — the counting, and the report it prints.
//
// See diskstats.h for what this class is and why it exists. The rule that
// governs every line below: the real device's result is what the caller gets,
// and the counting happens around the call, never inside it. The clock is
// read immediately before and immediately after the real transfer and nothing
// else sits between them, so the timings are the card's and not this layer's.
//
#include "diskstats.h"

#include <circle/devicenameservice.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/util.h>

static const char From[] = "diskstats";

// Sector counts are turned into a bucket index by their highest set bit, so
// the buckets are 1, 2-3, 4-7, 8-15 and so on. The last one collects
// everything larger.
static inline unsigned HighestBit (u32 nValue)
{
	unsigned nBit = 0;
	while (nValue > 1)
	{
		nValue >>= 1;
		nBit++;
	}
	return nBit;
}

// Percentages are kept as tenths of a percent and printed as two integers.
// There is no floating point anywhere in this file: this code sits in front
// of every disk access and the report is the only place a fraction is wanted.
static inline unsigned PercentTenths (u64 nPart, u64 nWhole)
{
	if (nWhole == 0)
	{
		return 0;
	}
	return (unsigned) ((nPart * 1000 + nWhole / 2) / nWhole);
}

static inline unsigned MeanTenths (u64 nTotal, u64 nCount)
{
	if (nCount == 0)
	{
		return 0;
	}
	return (unsigned) ((nTotal * 10 + nCount / 2) / nCount);
}

CDiskStatsDevice::CDiskStatsDevice (const char *pDeviceName)
:	m_pDeviceName (pDeviceName),
	m_pDevice (0),
	m_ullOffset (0),
	m_nNextExpected (0),
	m_bHavePrevious (FALSE),
	m_nSequential (0),
	m_nJumpForward (0),
	m_nJumpBackward (0),
	m_nWindowNext (0),
	m_nWindowUsed (0),
	m_nWindowSame (0),
	m_nWindowInside (0),
	m_nWindowEdge (0),
	m_nWindowMiss (0),
	m_ullLastReportTicks (0),
	m_ullFirstTicks (0),
	m_bStarted (FALSE),
	m_bLegendPrinted (FALSE),
	m_nPrevReadRequests (0),
	m_nPrevReadSectors (0),
	m_nPrevWriteRequests (0),
	m_nPrevWriteSectors (0)
{
	ResetDir (&m_Read);
	ResetDir (&m_Write);

	memset (m_nJumpBucket, 0, sizeof m_nJumpBucket);
	memset (m_Window, 0, sizeof m_Window);
}

CDiskStatsDevice::~CDiskStatsDevice (void)
{
	m_pDevice = 0;
}

void CDiskStatsDevice::ResetDir (TDirStats *pDir)
{
	memset (pDir, 0, sizeof *pDir);
	pDir->nMinMicros = ~0U;
}

boolean CDiskStatsDevice::Install (void)
{
	CDeviceNameService *pNames = CDeviceNameService::Get ();
	if (pNames == 0)
	{
		return FALSE;
	}

	CDevice *pReal = pNames->GetDevice (m_pDeviceName, TRUE);
	if (pReal == 0 || pReal == this)
	{
		return FALSE;
	}

	// Unregister the name and register this object under it. RemoveDevice on
	// the name service drops the name entry only — the real device object is
	// left alone, still initialised, and is what every call below reaches.
	pNames->RemoveDevice (m_pDeviceName, TRUE);
	pNames->AddDevice (m_pDeviceName, this, TRUE);

	m_pDevice = pReal;

	m_ullFirstTicks = CTimer::GetClockTicks64 ();
	m_ullLastReportTicks = m_ullFirstTicks;
	m_bStarted = TRUE;

	return TRUE;
}

// ---------------------------------------------------------------------------
// Pass-through
// ---------------------------------------------------------------------------

u64 CDiskStatsDevice::Seek (u64 ullOffset)
{
	if (m_pDevice == 0)
	{
		return (u64) -1;
	}

	u64 ullResult = m_pDevice->Seek (ullOffset);
	if (ullResult != (u64) -1)
	{
		m_ullOffset = ullOffset;
	}
	return ullResult;
}

u64 CDiskStatsDevice::GetSize (void) const
{
	if (m_pDevice == 0)
	{
		return (u64) -1;
	}
	return m_pDevice->GetSize ();
}

int CDiskStatsDevice::IOCtl (unsigned long ulCmd, void *pData)
{
	if (m_pDevice == 0)
	{
		return -1;
	}
	return m_pDevice->IOCtl (ulCmd, pData);
}

boolean CDiskStatsDevice::RemoveDevice (void)
{
	if (m_pDevice == 0)
	{
		return FALSE;
	}

	if (!m_pDevice->RemoveDevice ())
	{
		return FALSE;
	}

	// The real device is gone, so this object must stop answering to its
	// name: a wrapper around nothing would fail every read for ever, where
	// an absent name lets the layer above report the card as not ready.
	CDeviceNameService *pNames = CDeviceNameService::Get ();
	if (pNames != 0)
	{
		pNames->RemoveDevice (m_pDeviceName, TRUE);
	}
	m_pDevice = 0;

	return TRUE;
}

int CDiskStatsDevice::Read (void *pBuffer, size_t nCount)
{
	if (m_pDevice == 0)
	{
		return -1;
	}

	u64 nStartSector = m_ullOffset / DISKSTATS_SECTOR_SIZE;
	u32 nSectors = (u32) (nCount / DISKSTATS_SECTOR_SIZE);

	unsigned nBefore = CTimer::GetClockTicks ();
	int nResult = m_pDevice->Read (pBuffer, nCount);
	unsigned nMicros = CTimer::GetClockTicks () - nBefore;

	if (nResult > 0)
	{
		m_ullOffset += (u64) nResult;
	}

	RecordTransfer (&m_Read, nStartSector, nSectors, nMicros, nResult >= 0);
	RecordSequence (nStartSector, nSectors);
	RecordWindow (nStartSector, nSectors);

	return nResult;
}

int CDiskStatsDevice::Write (const void *pBuffer, size_t nCount)
{
	if (m_pDevice == 0)
	{
		return -1;
	}

	u64 nStartSector = m_ullOffset / DISKSTATS_SECTOR_SIZE;
	u32 nSectors = (u32) (nCount / DISKSTATS_SECTOR_SIZE);

	unsigned nBefore = CTimer::GetClockTicks ();
	int nResult = m_pDevice->Write (pBuffer, nCount);
	unsigned nMicros = CTimer::GetClockTicks () - nBefore;

	if (nResult > 0)
	{
		m_ullOffset += (u64) nResult;
	}

	RecordTransfer (&m_Write, nStartSector, nSectors, nMicros, nResult >= 0);

	// A write invalidates what a cache would be holding, so it also breaks
	// the read-ahead prediction: the next read is not a continuation of
	// anything this layer has seen.
	m_bHavePrevious = FALSE;

	return nResult;
}

// ---------------------------------------------------------------------------
// Counting
// ---------------------------------------------------------------------------

unsigned CDiskStatsDevice::SizeBucket (u32 nSectors)
{
	if (nSectors == 0)
	{
		return 0;
	}

	unsigned nBucket = HighestBit (nSectors);
	if (nBucket >= DISKSTATS_SIZE_BUCKETS)
	{
		nBucket = DISKSTATS_SIZE_BUCKETS - 1;
	}
	return nBucket;
}

unsigned CDiskStatsDevice::JumpBucket (u64 nDistanceSectors)
{
	if (nDistanceSectors < 8)		return 0;
	if (nDistanceSectors < 64)		return 1;
	if (nDistanceSectors < 512)		return 2;
	if (nDistanceSectors < 4096)		return 3;
	if (nDistanceSectors < 32768)		return 4;
	return 5;
}

void CDiskStatsDevice::RecordTransfer (TDirStats *pDir, u64 /* nStartSector */,
				       u32 nSectors, unsigned nMicros, boolean bOK)
{
	pDir->nRequests++;
	pDir->nSectors += nSectors;
	pDir->nSizeBucket[SizeBucket (nSectors)]++;

	if (!bOK)
	{
		pDir->nFailures++;
		return;		// a failed transfer's duration says nothing useful
	}

	pDir->nTotalMicros += nMicros;
	if (nMicros < pDir->nMinMicros)
	{
		pDir->nMinMicros = nMicros;
	}
	if (nMicros > pDir->nMaxMicros)
	{
		pDir->nMaxMicros = nMicros;
	}
}

void CDiskStatsDevice::RecordSequence (u64 nStartSector, u32 nSectors)
{
	if (m_bHavePrevious)
	{
		if (nStartSector == m_nNextExpected)
		{
			m_nSequential++;
		}
		else if (nStartSector > m_nNextExpected)
		{
			m_nJumpForward++;
			m_nJumpBucket[JumpBucket (nStartSector - m_nNextExpected)]++;
		}
		else
		{
			m_nJumpBackward++;
			m_nJumpBucket[JumpBucket (m_nNextExpected - nStartSector)]++;
		}
	}

	m_nNextExpected = nStartSector + nSectors;
	m_bHavePrevious = TRUE;
}

// The cache that is not here. Every read is checked against the ranges asked
// for recently and then joins them, oldest first out. Nothing but the range
// is kept, so this costs a fixed scan and no memory beyond the ring itself.
void CDiskStatsDevice::RecordWindow (u64 nStartSector, u32 nSectors)
{
	u64 nEnd = nStartSector + nSectors;

	boolean bSame = FALSE;
	boolean bInside = FALSE;
	boolean bEdge = FALSE;

	for (unsigned i = 0; i < m_nWindowUsed; i++)
	{
		const TRange &Entry = m_Window[i];
		u64 nEntryEnd = Entry.nStartSector + Entry.nSectors;

		if (Entry.nStartSector == nStartSector && Entry.nSectors == nSectors)
		{
			bSame = TRUE;
			break;
		}

		if (Entry.nStartSector <= nStartSector && nEnd <= nEntryEnd)
		{
			bInside = TRUE;
		}
		else if (nStartSector < nEntryEnd && Entry.nStartSector < nEnd)
		{
			bEdge = TRUE;
		}
	}

	if (bSame)		m_nWindowSame++;
	else if (bInside)	m_nWindowInside++;
	else if (bEdge)		m_nWindowEdge++;
	else			m_nWindowMiss++;

	m_Window[m_nWindowNext].nStartSector = nStartSector;
	m_Window[m_nWindowNext].nSectors = nSectors;
	m_nWindowNext = (m_nWindowNext + 1) % DISKSTATS_WINDOW_ENTRIES;
	if (m_nWindowUsed < DISKSTATS_WINDOW_ENTRIES)
	{
		m_nWindowUsed++;
	}
}

// ---------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------

void CDiskStatsDevice::Poll (void)
{
	if (!m_bStarted)
	{
		m_ullFirstTicks = CTimer::GetClockTicks64 ();
		m_ullLastReportTicks = m_ullFirstTicks;
		m_bStarted = TRUE;
		return;
	}

	u64 ullNow = CTimer::GetClockTicks64 ();
	if (ullNow - m_ullLastReportTicks < (u64) DISKSTATS_REPORT_SECONDS * CLOCKHZ)
	{
		return;
	}

	Report ();
}


void CDiskStatsDevice::ReportLegend (void)
{
	CLogger *pLog = CLogger::Get ();

	pLog->Write (From, LogNotice,
		     "measuring only — nothing is cached, and every request reaches the card "
		     "unchanged");
	pLog->Write (From, LogNotice,
		     "one sector is %u bytes, and every size and distance below is in sectors",
		     (unsigned) DISKSTATS_SECTOR_SIZE);
	pLog->Write (From, LogNotice,
		     "sizes  = how many requests were 1 sector long, how many were 2-3, and so on");
	pLog->Write (From, LogNotice,
		     "seq    = reads beginning exactly where the previous read ended, which is "
		     "what a read-ahead would already be holding; jump = the rest, and how far "
		     "from that point they began");
	pLog->Write (From, LogNotice,
		     "window = the last %u ranges asked for, kept as ranges and never as data. "
		     "same is that exact range asked for again, inside is fully covered by one "
		     "of them, edge is partly covered",
		     (unsigned) DISKSTATS_WINDOW_ENTRIES);
	pLog->Write (From, LogNotice,
		     "time   = microseconds spent inside the card driver, per request");
}

void CDiskStatsDevice::Report (void)
{
	CLogger *pLog = CLogger::Get ();
	if (pLog == 0)
	{
		return;
	}

	u64 ullNow = CTimer::GetClockTicks64 ();
	unsigned nIntervalSeconds =
		(unsigned) ((ullNow - m_ullLastReportTicks + CLOCKHZ / 2) / CLOCKHZ);
	unsigned nUptimeSeconds = (unsigned) ((ullNow - m_ullFirstTicks) / CLOCKHZ);

	m_ullLastReportTicks = ullNow;

	u64 nNewReads = m_Read.nRequests - m_nPrevReadRequests;
	u64 nNewReadSectors = m_Read.nSectors - m_nPrevReadSectors;
	u64 nNewWrites = m_Write.nRequests - m_nPrevWriteRequests;
	u64 nNewWriteSectors = m_Write.nSectors - m_nPrevWriteSectors;

	m_nPrevReadRequests = m_Read.nRequests;
	m_nPrevReadSectors = m_Read.nSectors;
	m_nPrevWriteRequests = m_Write.nRequests;
	m_nPrevWriteSectors = m_Write.nSectors;

	// A quiet interval gets one line. There is nothing new to describe, and
	// the report is polled serial output: printing the same eight lines at an
	// idle card would cost the machine more than the information is worth.
	if (nNewReads == 0 && nNewWrites == 0)
	{
		pLog->Write (From, LogNotice,
			     "t+%us  idle, no disk traffic  (so far: %llu reads / %llu KB, "
			     "%llu writes / %llu KB)",
			     nUptimeSeconds,
			     (unsigned long long) m_Read.nRequests,
			     (unsigned long long) (m_Read.nSectors * DISKSTATS_SECTOR_SIZE / 1024),
			     (unsigned long long) m_Write.nRequests,
			     (unsigned long long) (m_Write.nSectors * DISKSTATS_SECTOR_SIZE / 1024));
		return;
	}

	if (!m_bLegendPrinted)
	{
		ReportLegend ();
		m_bLegendPrinted = TRUE;
	}

	// Reads. Everything is a running total since boot except the bracket,
	// which is this interval alone — the totals size a cache, the bracket
	// says what the game is doing right now.
	unsigned nReadRateKB = 0;
	if (nIntervalSeconds > 0)
	{
		nReadRateKB = (unsigned) (nNewReadSectors * DISKSTATS_SECTOR_SIZE
					  / 1024 / nIntervalSeconds);
	}
	unsigned nReadMeanTenths = MeanTenths (m_Read.nSectors, m_Read.nRequests);

	pLog->Write (From, LogNotice,
		     "t+%us  reads %llu / %llu KB, mean %u.%u sectors  "
		     "(last %us: %llu reqs, %llu KB, %u KB/s)",
		     nUptimeSeconds,
		     (unsigned long long) m_Read.nRequests,
		     (unsigned long long) (m_Read.nSectors * DISKSTATS_SECTOR_SIZE / 1024),
		     nReadMeanTenths / 10, nReadMeanTenths % 10,
		     nIntervalSeconds,
		     (unsigned long long) nNewReads,
		     (unsigned long long) (nNewReadSectors * DISKSTATS_SECTOR_SIZE / 1024),
		     nReadRateKB);

	pLog->Write (From, LogNotice,
		     "  sizes  1:%llu  2-3:%llu  4-7:%llu  8-15:%llu  16-31:%llu  "
		     "32-63:%llu  64-127:%llu  128+:%llu",
		     (unsigned long long) m_Read.nSizeBucket[0],
		     (unsigned long long) m_Read.nSizeBucket[1],
		     (unsigned long long) m_Read.nSizeBucket[2],
		     (unsigned long long) m_Read.nSizeBucket[3],
		     (unsigned long long) m_Read.nSizeBucket[4],
		     (unsigned long long) m_Read.nSizeBucket[5],
		     (unsigned long long) m_Read.nSizeBucket[6],
		     (unsigned long long) m_Read.nSizeBucket[7]);

	// Sequentiality. The denominator is every read that had a predecessor to
	// be judged against, so the very first read of a run counts for neither.
	u64 nJudged = m_nSequential + m_nJumpForward + m_nJumpBackward;
	u64 nJumped = m_nJumpForward + m_nJumpBackward;
	unsigned nSeqTenths = PercentTenths (m_nSequential, nJudged);
	pLog->Write (From, LogNotice,
		     "  seq    %llu of %llu (%u.%u%%)   jump %llu (%llu on, %llu back)  "
		     "<8:%llu <64:%llu <512:%llu <4K:%llu <32K:%llu 32K+:%llu",
		     (unsigned long long) m_nSequential,
		     (unsigned long long) nJudged,
		     nSeqTenths / 10, nSeqTenths % 10,
		     (unsigned long long) nJumped,
		     (unsigned long long) m_nJumpForward,
		     (unsigned long long) m_nJumpBackward,
		     (unsigned long long) m_nJumpBucket[0],
		     (unsigned long long) m_nJumpBucket[1],
		     (unsigned long long) m_nJumpBucket[2],
		     (unsigned long long) m_nJumpBucket[3],
		     (unsigned long long) m_nJumpBucket[4],
		     (unsigned long long) m_nJumpBucket[5]);

	// What a cache would have caught. The size given is what the remembered
	// ranges add up to: the memory a cache holding exactly them would need.
	u64 nWindowSectors = 0;
	for (unsigned i = 0; i < m_nWindowUsed; i++)
	{
		nWindowSectors += m_Window[i].nSectors;
	}

	u64 nHits = m_nWindowSame + m_nWindowInside;
	unsigned nHitTenths = PercentTenths (nHits, m_Read.nRequests);
	pLog->Write (From, LogNotice,
		     "  window same %llu, inside %llu, edge %llu, miss %llu — %u.%u%% of reads "
		     "served without the card, out of %llu KB of ranges",
		     (unsigned long long) m_nWindowSame,
		     (unsigned long long) m_nWindowInside,
		     (unsigned long long) m_nWindowEdge,
		     (unsigned long long) m_nWindowMiss,
		     nHitTenths / 10, nHitTenths % 10,
		     (unsigned long long) (nWindowSectors * DISKSTATS_SECTOR_SIZE / 1024));

	u64 nTimedReads = m_Read.nRequests - m_Read.nFailures;
	if (nTimedReads > 0)
	{
		pLog->Write (From, LogNotice,
			     "  time   min %uus, mean %uus, max %uus, %llu ms in the driver "
			     "so far",
			     m_Read.nMinMicros,
			     (unsigned) (m_Read.nTotalMicros / nTimedReads),
			     m_Read.nMaxMicros,
			     (unsigned long long) (m_Read.nTotalMicros / 1000));
	}

	// Writes get one line: they are rare here, and a cache in front of them
	// is a different design question from the one being measured.
	if (m_Write.nRequests > 0)
	{
		unsigned nWriteMeanTenths = MeanTenths (m_Write.nSectors, m_Write.nRequests);
		pLog->Write (From, LogNotice,
			     "  writes %llu / %llu KB, mean %u.%u sectors, %llu ms in the "
			     "driver  (last %us: %llu reqs, %llu KB)",
			     (unsigned long long) m_Write.nRequests,
			     (unsigned long long) (m_Write.nSectors * DISKSTATS_SECTOR_SIZE / 1024),
			     nWriteMeanTenths / 10, nWriteMeanTenths % 10,
			     (unsigned long long) (m_Write.nTotalMicros / 1000),
			     nIntervalSeconds,
			     (unsigned long long) nNewWrites,
			     (unsigned long long) (nNewWriteSectors * DISKSTATS_SECTOR_SIZE / 1024));
	}

	if (m_Read.nFailures != 0 || m_Write.nFailures != 0)
	{
		pLog->Write (From, LogError, "  %llu reads and %llu writes FAILED",
			     (unsigned long long) m_Read.nFailures,
			     (unsigned long long) m_Write.nFailures);
	}
}

/*++

MixCast - loopback ring buffer (render endpoint -> capture endpoint)

Based on Microsoft SimpleAudioSample (Windows-driver-samples, MS-PL).

Audio written by apps to the virtual SPEAKER endpoint is pushed into this
ring, and the virtual MICROPHONE endpoint pulls from it. Samples are stored
in a canonical format: 48 kHz, stereo, 32-bit signed integer (left-justified),
so the render pin (16-bit) and capture pin (32-bit) can differ.

Both streams are paced by the same QPC-based clock inside this driver, so
there is no clock drift between them - only DPC phase jitter, which the
priming/trim logic below absorbs.

Thread safety: every method except Initialize/Cleanup may run at
DISPATCH_LEVEL and takes an internal spin lock. Lock order is always
stream position lock -> loopback lock, never the reverse.

--*/

#ifndef _MIXCAST_LOOPBACK_H_
#define _MIXCAST_LOOPBACK_H_

#define LOOPBACK_POOLTAG            'BLCM'      // 'MCLB' in pool dumps
#define LOOPBACK_CHANNELS           2
#define LOOPBACK_SAMPLE_RATE        48000

// Ring capacity: 500 ms. Only the newest audio is ever kept.
#define LOOPBACK_CAPACITY_FRAMES    (LOOPBACK_SAMPLE_RATE / 2)

// Latency window (frames @ 48 kHz).
//  - Target: how much must be buffered before capture starts consuming
//    (absorbs the ~10 ms DPC phase difference between the two streams).
//  - Max: if more than this is buffered, the oldest audio is dropped
//    down to Target so latency can never creep up.
#define LOOPBACK_TARGET_FRAMES      (LOOPBACK_SAMPLE_RATE * 20 / 1000)  // 20 ms
#define LOOPBACK_MAX_FRAMES         (LOOPBACK_SAMPLE_RATE * 60 / 1000)  // 60 ms

typedef struct _LOOPBACK_STATS
{
    ULONGLONG   FramesWritten;
    ULONGLONG   FramesRead;
    ULONG       Underruns;      // capture wanted audio, ring was empty
    ULONG       Overruns;       // ring full, oldest audio overwritten
    ULONG       Trims;          // latency exceeded max, trimmed to target
    ULONG       FillFrames;     // current buffered frames
} LOOPBACK_STATS, *PLOOPBACK_STATS;

//
// NOTE: no constructor on purpose. Kernel drivers do not run global C++
// constructors, so the global instance relies on zero-initialisation and
// an explicit Initialize() from DriverEntry.
//
class CLoopbackBuffer
{
public:
    NTSTATUS    Initialize();
    VOID        Cleanup();

    // Render side: copy ByteCount bytes of interleaved PCM in Format into the ring.
    VOID        Write(_In_reads_bytes_(ByteCount) const BYTE* Source,
                      _In_ ULONG ByteCount,
                      _In_ const WAVEFORMATEX* Format);

    // Capture side: fill ByteCount bytes of interleaved PCM in Format from the ring.
    // Always fills the whole destination (silence where no audio is available).
    VOID        Read(_Out_writes_bytes_(ByteCount) BYTE* Destination,
                     _In_ ULONG ByteCount,
                     _In_ const WAVEFORMATEX* Format);

    VOID        Reset();
    VOID        GetStats(_Out_ PLOOPBACK_STATS Stats);

private:
    KSPIN_LOCK  m_Lock;
    LONG*       m_pFrames;          // LOOPBACK_CAPACITY_FRAMES * LOOPBACK_CHANNELS
    ULONG       m_ReadIdx;          // in frames
    ULONG       m_WriteIdx;         // in frames
    ULONG       m_FillFrames;
    BOOLEAN     m_bPriming;         // outputting silence until TARGET is buffered
    BOOLEAN     m_bInitialized;
    LOOPBACK_STATS m_Stats;
};

extern CLoopbackBuffer g_Loopback;

#endif // _MIXCAST_LOOPBACK_H_

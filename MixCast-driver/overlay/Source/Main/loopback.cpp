/*++

MixCast - loopback ring buffer implementation.

Based on Microsoft SimpleAudioSample (Windows-driver-samples, MS-PL).

--*/

#include "definitions.h"
#include <ks.h>
#include <ksmedia.h>
#include "loopback.h"

// The one shared ring, zero-initialised at load; set up in DriverEntry.
CLoopbackBuffer g_Loopback;

//=============================================================================
// Sample conversion helpers (canonical = 32-bit signed, left-justified).
// Integer PCM only; container sizes 2, 3 and 4 bytes are supported.
//=============================================================================
#pragma code_seg()
static __forceinline LONG LoadSample(_In_ const BYTE* p, _In_ ULONG containerBytes)
{
    switch (containerBytes)
    {
    case 2:  return ((LONG)*(const SHORT UNALIGNED*)p) * 65536;
    case 3:  return (LONG)(((ULONG)p[0] << 8) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 24));
    case 4:  return *(const LONG UNALIGNED*)p;
    default: return 0;
    }
}

#pragma code_seg()
static __forceinline VOID StoreSample(_Out_ BYTE* p, _In_ ULONG containerBytes, _In_ LONG v)
{
    switch (containerBytes)
    {
    case 2:  *(SHORT UNALIGNED*)p = (SHORT)(v >> 16); break;
    case 3:  p[0] = (BYTE)(v >> 8); p[1] = (BYTE)(v >> 16); p[2] = (BYTE)(v >> 24); break;
    case 4:  *(LONG UNALIGNED*)p = v; break;
    default: break;
    }
}

//=============================================================================
#pragma code_seg("PAGE")
NTSTATUS CLoopbackBuffer::Initialize()
{
    PAGED_CODE();

    KeInitializeSpinLock(&m_Lock);

    m_pFrames = (LONG*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        (SIZE_T)LOOPBACK_CAPACITY_FRAMES * LOOPBACK_CHANNELS * sizeof(LONG),
        LOOPBACK_POOLTAG);

    if (m_pFrames == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    m_ReadIdx     = 0;
    m_WriteIdx    = 0;
    m_FillFrames  = 0;
    m_bPriming    = TRUE;
    RtlZeroMemory(&m_Stats, sizeof(m_Stats));
    m_bInitialized = TRUE;

    return STATUS_SUCCESS;
}

//=============================================================================
#pragma code_seg("PAGE")
VOID CLoopbackBuffer::Cleanup()
{
    PAGED_CODE();

    m_bInitialized = FALSE;

    if (m_pFrames != NULL)
    {
        ExFreePoolWithTag(m_pFrames, LOOPBACK_POOLTAG);
        m_pFrames = NULL;
    }
}

//=============================================================================
#pragma code_seg()
VOID CLoopbackBuffer::Reset()
{
    if (!m_bInitialized) return;

    KIRQL oldIrql;
    KeAcquireSpinLock(&m_Lock, &oldIrql);
    m_ReadIdx    = 0;
    m_WriteIdx   = 0;
    m_FillFrames = 0;
    m_bPriming   = TRUE;
    KeReleaseSpinLock(&m_Lock, oldIrql);
}

//=============================================================================
#pragma code_seg()
VOID CLoopbackBuffer::GetStats(_Out_ PLOOPBACK_STATS Stats)
{
    RtlZeroMemory(Stats, sizeof(*Stats));
    if (!m_bInitialized) return;

    KIRQL oldIrql;
    KeAcquireSpinLock(&m_Lock, &oldIrql);
    *Stats = m_Stats;
    Stats->FillFrames = m_FillFrames;
    KeReleaseSpinLock(&m_Lock, oldIrql);
}

//=============================================================================
// Render endpoint -> ring
//=============================================================================
#pragma code_seg()
VOID CLoopbackBuffer::Write
(
    _In_reads_bytes_(ByteCount) const BYTE* Source,
    _In_ ULONG ByteCount,
    _In_ const WAVEFORMATEX* Format
)
{
    if (!m_bInitialized || Format->nChannels == 0 || Format->nBlockAlign == 0)
    {
        return;
    }

    const ULONG channels       = Format->nChannels;
    const ULONG blockAlign     = Format->nBlockAlign;
    const ULONG containerBytes = blockAlign / channels;
    const ULONG frames         = ByteCount / blockAlign;

    KIRQL oldIrql;
    KeAcquireSpinLock(&m_Lock, &oldIrql);

    for (ULONG f = 0; f < frames; f++)
    {
        const BYTE* frame = Source + (SIZE_T)f * blockAlign;
        LONG* dst = m_pFrames + (SIZE_T)m_WriteIdx * LOOPBACK_CHANNELS;

        for (ULONG ch = 0; ch < LOOPBACK_CHANNELS; ch++)
        {
            // Mono source is duplicated to both channels; >2 channels: first two kept.
            ULONG srcCh = (ch < channels) ? ch : 0;
            dst[ch] = LoadSample(frame + srcCh * containerBytes, containerBytes);
        }

        m_WriteIdx = (m_WriteIdx + 1) % LOOPBACK_CAPACITY_FRAMES;

        if (m_FillFrames == LOOPBACK_CAPACITY_FRAMES)
        {
            // Full: overwrite oldest.
            m_ReadIdx = (m_ReadIdx + 1) % LOOPBACK_CAPACITY_FRAMES;
            m_Stats.Overruns++;
        }
        else
        {
            m_FillFrames++;
        }
    }

    m_Stats.FramesWritten += frames;

    KeReleaseSpinLock(&m_Lock, oldIrql);
}

//=============================================================================
// Ring -> capture endpoint
//=============================================================================
#pragma code_seg()
VOID CLoopbackBuffer::Read
(
    _Out_writes_bytes_(ByteCount) BYTE* Destination,
    _In_ ULONG ByteCount,
    _In_ const WAVEFORMATEX* Format
)
{
    if (!m_bInitialized || Format->nChannels == 0 || Format->nBlockAlign == 0)
    {
        RtlZeroMemory(Destination, ByteCount);
        return;
    }

    const ULONG channels       = Format->nChannels;
    const ULONG blockAlign     = Format->nBlockAlign;
    const ULONG containerBytes = blockAlign / channels;
    const ULONG frames         = ByteCount / blockAlign;

    KIRQL oldIrql;
    KeAcquireSpinLock(&m_Lock, &oldIrql);

    // 1) Latency guard: never let buffered audio grow past MAX.
    if (m_FillFrames > LOOPBACK_MAX_FRAMES)
    {
        ULONG drop = m_FillFrames - LOOPBACK_TARGET_FRAMES;
        m_ReadIdx = (m_ReadIdx + drop) % LOOPBACK_CAPACITY_FRAMES;
        m_FillFrames -= drop;
        m_Stats.Trims++;
        m_bPriming = FALSE;
    }

    // 2) Priming: stay silent until TARGET is buffered, so we don't
    //    alternate audio/silence every DPC when the ring is nearly empty.
    if (m_bPriming && m_FillFrames >= LOOPBACK_TARGET_FRAMES)
    {
        m_bPriming = FALSE;
    }

    ULONG available = m_bPriming ? 0 : min(m_FillFrames, frames);

    for (ULONG f = 0; f < available; f++)
    {
        BYTE* frame = Destination + (SIZE_T)f * blockAlign;
        const LONG* src = m_pFrames + (SIZE_T)m_ReadIdx * LOOPBACK_CHANNELS;

        if (channels == 1)
        {
            // Stereo -> mono downmix.
            StoreSample(frame, containerBytes, (src[0] / 2) + (src[1] / 2));
        }
        else
        {
            for (ULONG ch = 0; ch < channels; ch++)
            {
                LONG v = (ch < LOOPBACK_CHANNELS) ? src[ch] : 0;
                StoreSample(frame + ch * containerBytes, containerBytes, v);
            }
        }

        m_ReadIdx = (m_ReadIdx + 1) % LOOPBACK_CAPACITY_FRAMES;
    }

    m_FillFrames -= available;
    m_Stats.FramesRead += available;

    // 3) Underrun: pad with silence and re-enter priming.
    if (available < frames)
    {
        RtlZeroMemory(Destination + (SIZE_T)available * blockAlign,
                      ByteCount - available * blockAlign);

        if (!m_bPriming)
        {
            m_bPriming = TRUE;
            m_Stats.Underruns++;
        }
    }
    else if (ByteCount > frames * blockAlign)
    {
        // Partial trailing frame (should not happen at 48 kHz) -> silence.
        RtlZeroMemory(Destination + (SIZE_T)frames * blockAlign,
                      ByteCount - frames * blockAlign);
    }

    KeReleaseSpinLock(&m_Lock, oldIrql);
}

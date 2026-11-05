#include "sound_ace.h"

#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/logger.h>
#include <circle/util.h>

#define SAMPLE_RATE      48000
#define CHUNK_SIZE       (384 * 10)
#define QUEUE_MSECS      100
#define PREFILL_MSECS    40

#define Z80_CLOCK        3250000.0f
#define TSTATES_PER_SAMPLE (Z80_CLOCK / SAMPLE_RATE)

#define AMPLITUDE        8000.0f
#define AY_AMPLITUDE     6000.0f        // one channel at full volume
#define AY_STEP_TSTATES  8.0f           // CAY8910::Step(): 8 clock cycles (3.25 MHz)
#define DC_BLOCK_R       0.995f

static const char FromSound[] = "sound";

CAceSound::CAceSound(void)
:   m_pSound(nullptr),
    m_nEvents(0),
    m_nLevel(0),
    m_nFrameLevel(0),
    m_fSamplePos(0.0f),
    m_fFilterIn(0.0f),
    m_fFilterOut(0.0f),
    m_nAYEvents(0),
    m_bAYUsed(FALSE),
    m_fAYTime(0.0f)
{
    m_Events = new TEvent[MaxEvents];
    m_AYEvents = new TAYEvent[MaxAYEvents];
    m_Buffer = new s16[MaxFrameSamples * 2];
    memset(m_Buffer, 0, MaxFrameSamples * 2 * sizeof(s16));

    for (unsigned i = 0; i < 2; i++) {
        m_fAYFilterIn[i] = m_fAYFilterOut[i] = 0.0f;
    }
}

void CAceSound::AYReset(void)
{
    m_AY.Reset();
    m_nAYEvents = 0;
    m_bAYUsed = FALSE;
}

CAceSound::~CAceSound(void)
{
    if (m_pSound != nullptr) {
        m_pSound->Cancel();
        delete m_pSound;
        m_pSound = nullptr;
    }
    delete [] m_Events;
    delete [] m_AYEvents;
    delete [] m_Buffer;
}

boolean CAceSound::Initialize(CInterruptSystem *pInterrupt, const char *pDevice)
{
    if (strcmp(pDevice, "sndhdmi") == 0) {
        m_pSound = new CHDMISoundBaseDevice(pInterrupt, SAMPLE_RATE, CHUNK_SIZE);
    } else {
        pDevice = "sndpwm";
        m_pSound = new CPWMSoundBaseDevice(pInterrupt, SAMPLE_RATE, CHUNK_SIZE);
    }

    if (m_pSound == nullptr || !m_pSound->AllocateQueue(QUEUE_MSECS)) {
        CLogger::Get()->Write(FromSound, LogError, "Cannot allocate sound queue");
        delete m_pSound;
        m_pSound = nullptr;
        return FALSE;
    }

    m_pSound->SetWriteFormat(SoundFormatSigned16, 2);

    // Start with some silence so small timing jitter doesn't underrun the device
    memset(m_Buffer, 0, MaxFrameSamples * 2 * sizeof(s16));
    unsigned nPrefill = SAMPLE_RATE * PREFILL_MSECS / 1000;
    while (nPrefill > 0) {
        unsigned n = nPrefill < MaxFrameSamples ? nPrefill : MaxFrameSamples;
        m_pSound->Write(m_Buffer, n * 2 * sizeof(s16));
        nPrefill -= n;
    }

    if (!m_pSound->Start()) {
        CLogger::Get()->Write(FromSound, LogError, "Cannot start sound device %s", pDevice);
        delete m_pSound;
        m_pSound = nullptr;
        return FALSE;
    }

    CLogger::Get()->Write(FromSound, LogNotice, "Sound on %s", pDevice);

    return TRUE;
}

void CAceSound::EndFrame(unsigned long nFrameTStates)
{
    const float fFrameLen = (float) nFrameTStates;
    const float T = TSTATES_PER_SAMPLE;

    unsigned nLevel = m_nFrameLevel;
    unsigned nEvent = 0;
    unsigned nAYEvent = 0;
    unsigned nSamples = 0;
    float t = m_fSamplePos;

    // Box-filter the speaker level over each output sample period
    while (t + T <= fFrameLen && nSamples < MaxFrameSamples) {
        float fEnd = t + T;
        float fCur = t;
        float fAcc = 0.0f;

        while (nEvent < m_nEvents && (float) m_Events[nEvent].nTStates < fEnd) {
            float fEdge = (float) m_Events[nEvent].nTStates;
            if (fEdge > fCur) {
                fAcc += nLevel * (fEdge - fCur);
                fCur = fEdge;
            }
            nLevel = m_Events[nEvent].nLevel;
            nEvent++;
        }
        fAcc += nLevel * (fEnd - fCur);

        // Remove the DC offset so an idle speaker is silent at either level
        float x = fAcc / T;
        float y = x - m_fFilterIn + DC_BLOCK_R * m_fFilterOut;
        m_fFilterIn  = x;
        m_fFilterOut = y;
        float fLeft = y * AMPLITUDE;
        float fRight = fLeft;

        // The sound chip, averaged over its steps in this sample
        if (m_bAYUsed) {
            float fA = 0.0f, fB = 0.0f, fC = 0.0f;
            unsigned nSteps = 0;
            for (; m_fAYTime < fEnd; m_fAYTime += AY_STEP_TSTATES) {
                while (nAYEvent < m_nAYEvents
                       && (float) m_AYEvents[nAYEvent].nTStates <= m_fAYTime) {
                    m_AY.Write(m_AYEvents[nAYEvent].nRegister, m_AYEvents[nAYEvent].ucValue);
                    nAYEvent++;
                }
                m_AY.Step();
                fA += m_AY.GetChannel(0);
                fB += m_AY.GetChannel(1);
                fC += m_AY.GetChannel(2);
                nSteps++;
            }
            if (nSteps > 0) {
                float Side[2] = {(fA + 0.5f * fC) / nSteps, (fB + 0.5f * fC) / nSteps};
                for (unsigned i = 0; i < 2; i++) {
                    float fOut = Side[i] - m_fAYFilterIn[i] + DC_BLOCK_R * m_fAYFilterOut[i];
                    m_fAYFilterIn[i] = Side[i];
                    m_fAYFilterOut[i] = fOut;
                }
            }
            fLeft += m_fAYFilterOut[0] * AY_AMPLITUDE;
            fRight += m_fAYFilterOut[1] * AY_AMPLITUDE;
        }

        m_Buffer[nSamples * 2]     = (s16) fLeft;
        m_Buffer[nSamples * 2 + 1] = (s16) fRight;
        nSamples++;
        t = fEnd;
    }

    // Writes not reached (after the last sample) take effect now
    for (; nAYEvent < m_nAYEvents; nAYEvent++) {
        m_AY.Write(m_AYEvents[nAYEvent].nRegister, m_AYEvents[nAYEvent].ucValue);
    }
    m_nAYEvents = 0;
    // In step with the samples (when the chip is idle, from where they start)
    m_fAYTime = (m_bAYUsed ? m_fAYTime : t) - fFrameLen;

    m_fSamplePos = t - fFrameLen;
    m_nFrameLevel = m_nLevel;
    m_nEvents = 0;

    if (m_pSound == nullptr || nSamples == 0) {
        return;
    }

    // If the queue is full we are running ahead of the audio clock: drop the frame
    if (m_pSound->GetQueueFramesAvail() + nSamples <= m_pSound->GetQueueSizeFrames()) {
        m_pSound->Write(m_Buffer, nSamples * 2 * sizeof(s16));
    }
}

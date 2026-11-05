#ifndef _SOUND_ACE_H
#define _SOUND_ACE_H

#include <circle/interrupt.h>
#include <circle/sound/soundbasedevice.h>
#include <circle/types.h>
#include "ay8910.h"

/*
 * Jupiter Ace beeper: an IN from port 0xFE pulls the speaker low,
 * an OUT to port 0xFE drives it high. Edges are timestamped in Z80
 * t-states during a frame and resampled at the end of each frame.
 *
 * With the Jupiter II Expansion, the AY-3-8910 sound chip too: its register writes
 * are timestamped the same way and the chip is run at the end of each frame. The
 * output is stereo, as on the Expansion board: channel A on the left, B on the right,
 * C and the beeper in both.
 */
class CAceSound {
public:
    CAceSound(void);
    ~CAceSound(void);

    // pDevice: "sndpwm" (headphone jack, default) or "sndhdmi"
    boolean Initialize(CInterruptSystem *pInterrupt, const char *pDevice);

    void Speaker(unsigned nLevel, unsigned long nTStates)
    {
        if (nLevel != m_nLevel && m_nEvents < MaxEvents) {
            m_Events[m_nEvents].nTStates = nTStates;
            m_Events[m_nEvents].nLevel   = nLevel;
            m_nEvents++;
            m_nLevel = nLevel;
        }
    }

    // A write to a register of the sound chip, and a reset of it
    void AYWrite(unsigned nRegister, u8 ucValue, unsigned long nTStates)
    {
        if (m_nAYEvents < MaxAYEvents) {
            m_AYEvents[m_nAYEvents].nTStates  = nTStates;
            m_AYEvents[m_nAYEvents].nRegister = nRegister;
            m_AYEvents[m_nAYEvents].ucValue   = ucValue;
            m_nAYEvents++;
        }
        m_bAYUsed = TRUE;
    }
    void AYReset(void);

    // Render one emulated frame of nFrameTStates and queue it for playback
    void EndFrame(unsigned long nFrameTStates);

private:
    static const unsigned MaxEvents = 8192;
    static const unsigned MaxAYEvents = 4096;
    static const unsigned MaxFrameSamples = 2048;

    struct TEvent {
        unsigned long nTStates;
        unsigned      nLevel;
    };

    struct TAYEvent {
        unsigned long nTStates;
        unsigned      nRegister;
        u8            ucValue;
    };

    CSoundBaseDevice *m_pSound;

    // The buffers come from the heap: CKernel, which holds this, is on the startup stack
    TEvent  *m_Events;          // MaxEvents
    unsigned m_nEvents;
    unsigned m_nLevel;          // current speaker level (last recorded)
    unsigned m_nFrameLevel;     // speaker level at the start of the frame

    float    m_fSamplePos;      // start of the next sample, in t-states from frame start
    float    m_fFilterIn;       // DC blocker state
    float    m_fFilterOut;

    CAY8910  m_AY;
    TAYEvent *m_AYEvents;       // MaxAYEvents
    unsigned m_nAYEvents;
    boolean  m_bAYUsed;         // written since the last reset: run it
    float    m_fAYTime;         // the chip's next step, in t-states from frame start
    float    m_fAYFilterIn[2];  // DC blockers, left and right
    float    m_fAYFilterOut[2];

    s16     *m_Buffer;          // MaxFrameSamples * 2: stereo
};

#endif

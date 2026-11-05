#ifndef _AY8910_H
#define _AY8910_H

#include <circle/types.h>

/*
 * The AY-3-8910's sound generators: three square wave tone channels (A, B, C), a
 * noise generator and an envelope generator. Step() advances the chip by 8 clock
 * cycles (the tone counters' rate); a tone of period P has a frequency of
 * clock / (16 P), the noise is shifted every period of clock / 16 P, and the envelope
 * takes a step every 16 P clock cycles (16 steps). The outputs are the channels'
 * levels, 0.0 - 1.0 on the chip's logarithmic volume scale.
 */
class CAY8910 {
public:
    CAY8910(void);

    void Reset(void);
    void Write(unsigned nRegister, u8 ucValue);

    // 8 clock cycles
    void Step(void);

    float GetChannel(unsigned nChannel) const;     // 0 - 2: A, B, C

private:
    u8       m_Regs[16];

    unsigned m_nToneCount[3];
    unsigned m_bToneOut[3];

    unsigned m_nNoisePrescale;          // the noise runs at half the tone rate
    unsigned m_nNoiseCount;
    u32      m_nNoiseShift;             // 17-bit shift register

    unsigned m_nEnvCount;
    unsigned m_nEnvStep;                // 0 - 15 in the current ramp
    boolean  m_bEnvAttack;              // ramping up
    boolean  m_bEnvHolding;
    unsigned m_nEnvVolume;              // 0 - 15
};

#endif

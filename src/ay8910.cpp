#include "ay8910.h"
#include <string.h>

// Output level of the 16 volume steps (about 3 dB apart, the first silent)
static const float Volume[16] = {
    0.0f,     0.00999f, 0.01445f, 0.02105f, 0.03070f, 0.04555f, 0.06450f, 0.10736f,
    0.12659f, 0.20498f, 0.29221f, 0.37274f, 0.49261f, 0.63532f, 0.80558f, 1.0f
};

#define REG_ENABLE      7
#define REG_VOLUME_A    8
#define REG_ENV_LOW     11
#define REG_ENV_HIGH    12
#define REG_ENV_SHAPE   13

#define SHAPE_HOLD      0x01
#define SHAPE_ALTERNATE 0x02
#define SHAPE_ATTACK    0x04
#define SHAPE_CONTINUE  0x08

CAY8910::CAY8910(void)
{
    Reset();
}

void CAY8910::Reset(void)
{
    memset(m_Regs, 0, sizeof m_Regs);
    for (unsigned i = 0; i < 3; i++) {
        m_nToneCount[i] = 0;
        m_bToneOut[i] = 0;
    }
    m_nNoisePrescale = 0;
    m_nNoiseCount = 0;
    m_nNoiseShift = 1;
    m_nEnvCount = 0;
    m_nEnvStep = 0;
    m_bEnvAttack = FALSE;
    m_bEnvHolding = TRUE;
    m_nEnvVolume = 0;
}

void CAY8910::Write(unsigned nRegister, u8 ucValue)
{
    if (nRegister >= 16) {
        return;
    }
    m_Regs[nRegister] = ucValue;

    if (nRegister == REG_ENV_SHAPE) {
        // A new shape starts a new envelope
        m_nEnvCount = 0;
        m_nEnvStep = 0;
        m_bEnvAttack = (ucValue & SHAPE_ATTACK) != 0;
        m_bEnvHolding = FALSE;
        m_nEnvVolume = m_bEnvAttack ? 0 : 15;
    }
}

void CAY8910::Step(void)
{
    // Tones: the output flips every period
    for (unsigned i = 0; i < 3; i++) {
        unsigned nPeriod = m_Regs[i * 2] | ((m_Regs[i * 2 + 1] & 0x0F) << 8);
        if (nPeriod == 0) {
            nPeriod = 1;
        }
        if (++m_nToneCount[i] >= nPeriod) {
            m_nToneCount[i] = 0;
            m_bToneOut[i] ^= 1;
        }
    }

    // Noise: a 17-bit shift register, bit 0 XOR bit 3 fed in at the top
    m_nNoisePrescale ^= 1;
    if (m_nNoisePrescale) {
        unsigned nPeriod = m_Regs[6] & 0x1F;
        if (nPeriod == 0) {
            nPeriod = 1;
        }
        if (++m_nNoiseCount >= nPeriod) {
            m_nNoiseCount = 0;
            u32 nBit = (m_nNoiseShift ^ (m_nNoiseShift >> 3)) & 1;
            m_nNoiseShift = (m_nNoiseShift >> 1) | (nBit << 16);
        }
    }

    // Envelope: one step every 2 * period here (16 * period clock cycles)
    if (!m_bEnvHolding) {
        unsigned nPeriod = m_Regs[REG_ENV_LOW] | (m_Regs[REG_ENV_HIGH] << 8);
        if (nPeriod == 0) {
            nPeriod = 1;
        }
        if (++m_nEnvCount >= 2 * nPeriod) {
            m_nEnvCount = 0;
            if (++m_nEnvStep < 16) {
                m_nEnvVolume = m_bEnvAttack ? m_nEnvStep : 15 - m_nEnvStep;
            } else {
                // The end of a ramp
                u8 ucShape = m_Regs[REG_ENV_SHAPE];
                if (!(ucShape & SHAPE_CONTINUE)) {
                    m_bEnvHolding = TRUE;           // shapes 0 - 7: then silent
                    m_nEnvVolume = 0;
                } else if (ucShape & SHAPE_HOLD) {
                    m_bEnvHolding = TRUE;           // held at the end, or the start
                    if (ucShape & SHAPE_ALTERNATE) {
                        m_bEnvAttack = !m_bEnvAttack;
                    }
                    m_nEnvVolume = m_bEnvAttack ? 15 : 0;
                } else {
                    if (ucShape & SHAPE_ALTERNATE) {
                        m_bEnvAttack = !m_bEnvAttack;   // triangle
                    }
                    m_nEnvStep = 0;                     // sawtooth
                    m_nEnvVolume = m_bEnvAttack ? 0 : 15;
                }
            }
        }
    }
}

float CAY8910::GetChannel(unsigned nChannel) const
{
    u8 ucEnable = m_Regs[REG_ENABLE];
    unsigned bTone  = m_bToneOut[nChannel] | ((ucEnable >> nChannel) & 1);
    unsigned bNoise = (m_nNoiseShift & 1) | ((ucEnable >> (nChannel + 3)) & 1);
    if (!(bTone & bNoise)) {
        return 0.0f;
    }
    u8 ucVolume = m_Regs[REG_VOLUME_A + nChannel];
    return Volume[(ucVolume & 0x10) ? m_nEnvVolume : (ucVolume & 0x0F)];
}

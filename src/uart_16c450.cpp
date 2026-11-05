#include "uart_16c450.h"
#include "machine.h"

#define UART_CLOCK_HZ   3686400
#define Z80_CLOCK_HZ    3250000

// Registers (DLAB is bit 7 of LCR)
#define REG_DATA        0       // RBR / THR, DLL with DLAB
#define REG_IER         1       // DLM with DLAB
#define REG_IIR         2
#define REG_LCR         3
#define REG_MCR         4
#define REG_LSR         5
#define REG_MSR         6
#define REG_SCR         7

#define LCR_DLAB        0x80
#define LSR_DR          0x01    // data ready
#define LSR_OE          0x02    // overrun
#define LSR_THRE        0x20    // transmit holding register empty
#define LSR_TEMT        0x40    // transmitter empty
#define MCR_LOOP        0x10

static const TUartHost *s_pHost = nullptr;

static u8 s_ucIER, s_ucLCR, s_ucMCR, s_ucSCR, s_ucDLL, s_ucDLM;
static u8 s_ucRBR;
static u8 s_ucLSR;
static u64 s_nTxBusyUntil;       // MachineTime() when the byte being sent is out
static u64 s_nRxNextAt;          // when the next byte can have arrived

void UartSetHost(const TUartHost *pHost)
{
    s_pHost = pHost;
}

static unsigned divisor(void)
{
    unsigned nDivisor = s_ucDLL | (s_ucDLM << 8);
    return nDivisor != 0 ? nDivisor : 0x10000;
}

// T-states a character takes on the line: start bit, data, parity, stop bits
static u64 char_tstates(void)
{
    unsigned nBits = 1 + 5 + (s_ucLCR & 3) + ((s_ucLCR & 0x08) ? 1 : 0)
                     + ((s_ucLCR & 0x04) ? 2 : 1);
    return (u64) nBits * 16 * divisor() * Z80_CLOCK_HZ / UART_CLOCK_HZ;
}

static void set_line(void)
{
    if (s_pHost == nullptr) {
        return;
    }
    unsigned nBaud = UART_CLOCK_HZ / 16 / divisor();
    unsigned nDataBits = 5 + (s_ucLCR & 3);
    unsigned nStopBits = (s_ucLCR & 0x04) ? 2 : 1;
    char chParity = 'N';
    if (s_ucLCR & 0x08) {
        static const char Parity[4] = {'O', 'E', 'M', 'S'};
        chParity = Parity[(s_ucLCR >> 4) & 3];
    }
    s_pHost->SetLine(nBaud, nDataBits, nStopBits, chParity);
}

void UartReset(void)
{
    s_ucIER = s_ucLCR = s_ucMCR = 0;
    s_ucSCR = 0;
    s_ucDLL = s_ucDLM = 0;
    s_ucRBR = 0;
    s_ucLSR = LSR_THRE | LSR_TEMT;
    s_nTxBusyUntil = 0;
    s_nRxNextAt = 0;
}

// Bring the line status up to now: a byte sent has gone, a byte may have arrived
static void update(void)
{
    u64 nNow = MachineTime();
    if (!(s_ucLSR & LSR_TEMT) && nNow >= s_nTxBusyUntil) {
        s_ucLSR |= LSR_THRE | LSR_TEMT;
    }
    if (s_pHost != nullptr && !(s_ucMCR & MCR_LOOP) && nNow >= s_nRxNextAt) {
        u8 ucByte;
        if (s_pHost->Receive(&ucByte)) {
            if (s_ucLSR & LSR_DR) {
                s_ucLSR |= LSR_OE;              // the last one was not read
            }
            s_ucRBR = ucByte;
            s_ucLSR |= LSR_DR;
            s_nRxNextAt = nNow + char_tstates();
        }
    }
}

u8 UartRead(unsigned nRegister)
{
    switch (nRegister) {
    case REG_DATA:
        if (s_ucLCR & LCR_DLAB) {
            return s_ucDLL;
        }
        update();
        s_ucLSR &= ~LSR_DR;
        return s_ucRBR;

    case REG_IER:
        return (s_ucLCR & LCR_DLAB) ? s_ucDLM : s_ucIER;

    case REG_IIR:
        return 0x01;                            // no interrupt pending

    case REG_LCR:
        return s_ucLCR;

    case REG_MCR:
        return s_ucMCR;

    case REG_LSR: {
        update();
        u8 ucLSR = s_ucLSR;
        s_ucLSR &= ~LSR_OE;                     // cleared by reading
        return ucLSR;
    }

    case REG_MSR:
        if (s_ucMCR & MCR_LOOP) {               // DTR -> DSR, RTS -> CTS, OUT1 -> RI, OUT2 -> DCD
            return ((s_ucMCR & 0x01) << 5) | ((s_ucMCR & 0x02) << 3)
                 | ((s_ucMCR & 0x04) << 4) | ((s_ucMCR & 0x08) << 4);
        }
        return 0x30;                            // CTS and DSR (pulled up on the board)

    default:
        return s_ucSCR;
    }
}

void UartWrite(unsigned nRegister, u8 ucValue)
{
    switch (nRegister) {
    case REG_DATA:
        if (s_ucLCR & LCR_DLAB) {
            s_ucDLL = ucValue;
            return;
        }
        update();
        if (s_ucMCR & MCR_LOOP) {
            s_ucRBR = ucValue;                  // loopback: straight to the receiver
            s_ucLSR |= LSR_DR;
            return;
        }
        if (s_pHost != nullptr) {
            s_pHost->Send(ucValue);
        }
        s_ucLSR &= ~(LSR_THRE | LSR_TEMT);
        s_nTxBusyUntil = MachineTime() + char_tstates();
        return;

    case REG_IER:
        if (s_ucLCR & LCR_DLAB) {
            s_ucDLM = ucValue;
        } else {
            s_ucIER = ucValue & 0x0F;
        }
        return;

    case REG_LCR: {
        boolean bDLABOff = (s_ucLCR & LCR_DLAB) && !(ucValue & LCR_DLAB);
        boolean bChanged = (s_ucLCR & 0x3F) != (ucValue & 0x3F);
        s_ucLCR = ucValue;
        if (!(ucValue & LCR_DLAB) && (bDLABOff || bChanged)) {
            set_line();                         // the speed is set once DLAB is cleared
        }
        return;
    }

    case REG_MCR:
        s_ucMCR = ucValue & 0x1F;
        return;

    case REG_SCR:
        s_ucSCR = ucValue;
        return;

    default:
        return;                                 // IIR, LSR, MSR: read only
    }
}

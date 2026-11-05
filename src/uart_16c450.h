#ifndef _UART_16C450_H
#define _UART_16C450_H

#include <circle/types.h>

/*
 * The Jupiter II Expansion's 16C450 UART (registers 0 - 7 at odd ports E1h - EFh),
 * clocked at 3.6864 MHz: baud = 230400 / divisor. Its interrupt line is not
 * connected, so only the registers matter. Bytes go to and come from a host serial
 * port (UartSetHost()); a byte takes as long to send as at the chosen speed, so a
 * program waiting for THR to empty is paced as on the real machine. With no host
 * port, bytes sent are lost and none arrive.
 */

struct TUartHost {
    // Set the line: baud rate, 5 - 8 data bits, 1 or 2 stop bits, parity
    // ('N', 'O', 'E', 'M' mark, 'S' space)
    void (*SetLine)(unsigned nBaud, unsigned nDataBits, unsigned nStopBits, char chParity);
    // A byte received, FALSE if none
    boolean (*Receive)(u8 *pByte);
    void (*Send)(u8 ucByte);
};

void UartSetHost(const TUartHost *pHost);       // nullptr: none

void UartReset(void);
u8 UartRead(unsigned nRegister);
void UartWrite(unsigned nRegister, u8 ucValue);

#endif

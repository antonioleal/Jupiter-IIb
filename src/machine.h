#ifndef _MACHINE_H
#define _MACHINE_H

#include <circle/types.h>

/*
 * The machine emulated (Ctrl+F12):
 *
 * Jupiter II (https://github.com/ricaflops/Jupiter-II): a Jupiter Ace with
 *   - 60 Hz (NTSC) video: 262 lines of 208 T-states (54,496 per frame, 59.64 Hz at
 *     3.25 MHz), the picture in lines 0-191, vsync (the Z80's INT) on lines 224-227;
 *   - 2K of RAM at 3000h - 37FFh, seen again at 3800h - 3FFFh (the Ace: 1K, 4 times);
 *   - 2K of screen RAM and 2K of character set RAM, each in two 1K pages chosen by
 *     the -SPAGE and -CPAGE lines of the video connector (page 1 without the
 *     Expansion board: the lines are pulled up);
 *   - a 16K boot ROM: the Ace ROM, and a copy patched at 008Eh (JP C000h) to start
 *     the Expansion ROM, chosen by a jumper.
 *
 * Jupiter II with its Expansion board (https://github.com/ricaflops/Jupiter-II-Expansion):
 *   - an AY-3-8910 sound chip at 3.25 MHz: OUT to odd ports 1111xx01b (FDh) selects
 *     a register, IN / OUT on odd ports 1111xx11b (FFh) read / write it. Its I/O
 *     ports (outputs when bits 6 / 7 of register 7 are set, else pulled up to FFh):
 *       port A (register 14): PAPER (bits 7-4) and INK (bits 3-0) colours
 *       port B (register 15): BORDER colour (bits 7-4), character set page (bit 3),
 *                             screen page (bit 2), memory page (bits 1-0)
 *     Colours are IRGB: bit 3 intensity, bits 2-0 red, green, blue.
 *   - colour: a 2K attribute RAM (one byte per character cell, for each screen page)
 *     written with port A whenever the CPU writes the screen RAM. A cell shows INK
 *     where its pixel (after inverse video) is set, PAPER elsewhere.
 *   - 128K RAM and the 16K Expansion ROM: 4000h - 7FFFh always the same 16K of RAM;
 *     8000h - FFFFh one of 4 pages; page 3 has the ROM at C000h - FFFFh. A reset
 *     leaves the sound chip's ports as inputs: page 3, character set 1, screen 1.
 *   - a 16C450 UART (3.6864 MHz clock) at odd ports 1110xxx1b (E1h - EFh), its
 *     register number in A3 - A1.
 *
 * Jupiter Ace: the original machine (50 Hz, 1K RAM at 3000h), with the RAM packs of
 * ram_ace.h. The RAM packs can also be used on the Jupiter II without the Expansion.
 */

enum TMachine {
    MachineJupiterIIExpansion,      // the default
    MachineJupiterII,
    MachineAce,
    MachineUnknown
};

// Choose the machine: timing, memory map and I/O. The ROM must then be loaded again
// (MachinePatchROM()) and the machine reset.
void MachineSet(TMachine Machine);
TMachine MachineGet(void);
const char *MachineName(TMachine Machine);

boolean MachineIsJupiterII(void);       // with or without the Expansion
boolean MachineHasExpansion(void);

// Frame timing
unsigned MachineFrameTStates(void);
unsigned MachineFrameMicros(void);      // real time a frame takes
unsigned MachineFrameRate(void);        // frames a second, rounded

// The ROM in mem[] (0000h - 1FFFh, the Ace ROM): the Expansion boot patch
void MachinePatchROM(u8 *pROM);

// Power-on / reset (after mem[] 2000h - FFFFh is set to FFh): the sound chip, the
// pages, the Expansion ROM and the attribute RAM
void MachineReset(void);

// End of a frame of nFrameTStates; T-states since power-on (the time UART transfers
// are paced by)
void MachineEndFrame(unsigned nFrameTStates);
u64 MachineTime(void);

// The first address above the RAM in the Z80's view (10000h with the Expansion)
unsigned MachineRamEnd(void);

// I/O on odd ports (even ones are the Ace's keyboard, speaker and tape): only the
// Expansion board has any. MachineIn() gives FFh where nothing answers.
u8 MachineIn(u16 nPort);
void MachineOut(u16 nPort, u8 ucValue);

// The sound chip's register writes, timed in T-states from the start of the frame,
// for the sound output (CAceSound); and a reset of it
typedef void TAYWriteHandler(unsigned nRegister, u8 ucValue, unsigned long nTStates);
typedef void TAYResetHandler(void);
void MachineSetAYHandlers(TAYWriteHandler *pWrite, TAYResetHandler *pReset);

// Colour (with the Expansion): the attribute of each cell of the screen page shown
// (PAPER << 4 | INK), the border colour, and a colour (0 - 15) as 0x00RRGGBB
const u8 *MachineAttributes(void);
unsigned MachineBorderColour(void);
u32 MachineColourRGB(unsigned nColour);

// The pages shown (0 or 1; 1 without the Expansion) and the memory page (0 - 3)
unsigned MachineScreenPage(void);
unsigned MachineCharsetPage(void);
unsigned MachineMemoryPage(void);

// A character set page (128 characters of 8 bytes)
const u8 *MachineCharset(unsigned nPage);

// Choose memory page nPage (0 - 3) as an OUT to port B would, the other bits kept
void MachineSelectMemoryPage(unsigned nPage);

// The Expansion's state for an .ace snapshot (tape_sd.cpp): the sound chip's
// registers, the pages not shown, the attribute RAM and all 4 memory pages.
// MachineGetState() gives a buffer of MACHINE_STATE_SIZE bytes (nullptr if there is
// no memory for it); MachineSetState()
// takes one back (the machine must be the Jupiter II with the Expansion).
#define MACHINE_STATE_VERSION   1
#define MACHINE_STATE_SIZE      (16 + 1 + 1 + 1 + 1024 + 1024 + 2048 + 4 * 0x8000)
const u8 *MachineGetState(void);
boolean MachineSetState(const u8 *pState);

#endif

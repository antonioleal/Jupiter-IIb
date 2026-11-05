#include "machine.h"
#include "ram_ace.h"
#include "uart_16c450.h"
#include "xrom.h"
#include <string.h>

extern "C" {
#include "z80.h"
extern unsigned long tstates;
extern unsigned long tsmax;
}

#define Z80_CLOCK_HZ        3250000
#define LINE_TSTATES        208

// The Ace: 312 lines, vsync at T-state 154 of line 248 for 8 lines (z80.c)
#define ACE_LINES           312
#define ACE_INT_TSTATE      (248 * LINE_TSTATES + 154)
#define ACE_INT_LOW         (8 * LINE_TSTATES)

// The Jupiter II (J-II_TIME.PLD): 262 lines, vsync from the start of line 224 for 4
#define J2_LINES            262
#define J2_INT_TSTATE       (224 * LINE_TSTATES)
#define J2_INT_LOW          (4 * LINE_TSTATES)

// The ROM patch that starts the Expansion ROM (Jupiter-II ROM notes): at 008Eh
// IM 1 / JR 009Bh becomes JP C000h / NOP; the Expansion ROM does the IM 1 itself
#define BOOT_PATCH_ADDR     0x008E

#define PAGE_START          0x8000      // memory pages: 8000h - FFFFh
#define PAGE_SIZE           0x8000
#define ROM_PAGE            3           // has the Expansion ROM at C000h
#define ROM_PAGE_RAM        0x4000      // and 16K of RAM at 8000h

#define VIDEO_PAGE_SIZE     0x0400

// AY-3-8910 registers
#define AY_ENABLE           7           // bit 6: port A output, bit 7: port B output
#define AY_PORT_A           14
#define AY_PORT_B           15

#define PORT_B_CHARSET      0x08
#define PORT_B_SCREEN       0x04
#define PORT_B_MEMORY       0x03

extern "C" {
// For the Z80 core (z80.c, z80.h)
unsigned long machine_frame_tstates = J2_LINES * LINE_TSTATES;
unsigned long machine_int_tstate = J2_INT_TSTATE;
unsigned long machine_int_low_tstates = J2_INT_LOW;
int machine_j2 = 1;
int machine_expansion = 1;
unsigned char machine_attr_colour = 0xFF;
unsigned char *machine_attr;
}

static TMachine s_Machine = MachineJupiterIIExpansion;
static u64 s_nFrameBase = 0;            // T-states before this frame

// The sound chip: registers as written, the register selected
static u8 s_AYRegs[16];
static u8 s_nAYAddress;
static TAYWriteHandler *s_pAYWrite = nullptr;
static TAYResetHandler *s_pAYReset = nullptr;

// The ports as the board sees them, and what they select now
static u8 s_ucPortA = 0xFF;
static u8 s_ucPortB = 0xFF;
static unsigned s_nScreenPage = 1;
static unsigned s_nCharsetPage = 1;
static unsigned s_nMemoryPage = ROM_PAGE;

// The screen and character set pages not in mem[] (2000h - 23FFh / 2400h - 27FFh,
// 2800h - 2BFFh / 2C00h - 2FFFh), the attribute RAM of both screen pages, and the
// memory pages (the one in use is in mem[] 8000h - FFFFh; page 3's second half is
// the Expansion ROM, never written back)
static u8 s_ScreenOther[VIDEO_PAGE_SIZE];
static u8 s_CharsetOther[VIDEO_PAGE_SIZE];
static u8 s_Attributes[2][VIDEO_PAGE_SIZE];
static u8 s_Pages[4][PAGE_SIZE];

static u8 *s_pState = nullptr;          // MachineGetState()'s buffer, made when first needed

static void apply_ports(void);

void MachineSet(TMachine Machine)
{
    if (Machine >= MachineUnknown) {
        Machine = MachineJupiterIIExpansion;
    }
    s_Machine = Machine;

    boolean bJ2 = Machine != MachineAce;
    unsigned nLines = bJ2 ? J2_LINES : ACE_LINES;
    machine_frame_tstates = nLines * LINE_TSTATES;
    machine_int_tstate = bJ2 ? J2_INT_TSTATE : ACE_INT_TSTATE;
    machine_int_low_tstates = bJ2 ? J2_INT_LOW : ACE_INT_LOW;
    tsmax = machine_frame_tstates;
    machine_j2 = bJ2;
    machine_expansion = Machine == MachineJupiterIIExpansion;
    machine_attr = s_Attributes[1];

    // The ROM in mem[]: the boot patch only with the Expansion
    static const u8 Original[] = {0xED, 0x56, 0x18, 0x09};     // IM 1 / JR 009Bh
    memcpy(mem + BOOT_PATCH_ADDR, Original, sizeof Original);
    MachinePatchROM(mem);

    if (machine_expansion) {
        // The Expansion's memory replaces any RAM pack
        ram_paging = 0;
        ram_latch_pending = 0;
        for (unsigned nBank = 2; nBank < 6; nBank++) {
            memattr[nBank] = 1;
        }
        memattr[6] = memattr[7] = s_nMemoryPage != ROM_PAGE;
    } else {
        RamSetPackKB(RamGetPackKB());
    }
}

TMachine MachineGet(void)
{
    return s_Machine;
}

const char *MachineName(TMachine Machine)
{
    switch (Machine) {
    case MachineJupiterIIExpansion: return "Jupiter II + Expansion";
    case MachineJupiterII:          return "Jupiter II";
    case MachineAce:                return "Jupiter Ace";
    default:                        return "?";
    }
}

boolean MachineIsJupiterII(void)
{
    return machine_j2 != 0;
}

boolean MachineHasExpansion(void)
{
    return machine_expansion != 0;
}

unsigned MachineFrameTStates(void)
{
    return machine_frame_tstates;
}

unsigned MachineFrameMicros(void)
{
    return (unsigned) ((u64) machine_frame_tstates * 1000000 / Z80_CLOCK_HZ);
}

unsigned MachineFrameRate(void)
{
    return (Z80_CLOCK_HZ + machine_frame_tstates / 2) / machine_frame_tstates;
}

void MachinePatchROM(u8 *pROM)
{
    if (machine_expansion) {
        pROM[BOOT_PATCH_ADDR]     = 0xC3;       // JP C000h
        pROM[BOOT_PATCH_ADDR + 1] = 0x00;
        pROM[BOOT_PATCH_ADDR + 2] = 0xC0;
        pROM[BOOT_PATCH_ADDR + 3] = 0x00;       // NOP
    }
}

void MachineReset(void)
{
    s_nFrameBase = 0;

    // The sound chip clears its registers: its ports become inputs (FFh)
    memset(s_AYRegs, 0, sizeof s_AYRegs);
    s_nAYAddress = 0;
    if (s_pAYReset != nullptr) {
        s_pAYReset();
    }

    memset(s_ScreenOther, 0xFF, sizeof s_ScreenOther);
    memset(s_CharsetOther, 0xFF, sizeof s_CharsetOther);
    memset(s_Attributes, 0xFF, sizeof s_Attributes);
    memset(s_Pages, 0xFF, sizeof s_Pages);
    memcpy(s_Pages[ROM_PAGE] + ROM_PAGE_RAM, xrom, sizeof xrom);

    // mem[] holds the pages a reset selects (page 1 of the screen and character set,
    // memory page 3); 8000h - FFFFh is set from page 3
    s_nScreenPage = 1;
    s_nCharsetPage = 1;
    s_nMemoryPage = ROM_PAGE;
    machine_attr = s_Attributes[1];
    if (machine_expansion) {
        memcpy(mem + PAGE_START, s_Pages[ROM_PAGE], PAGE_SIZE);
        memattr[6] = memattr[7] = 0;
    }
    s_ucPortA = s_ucPortB = 0xFF;
    machine_attr_colour = 0xFF;

    UartReset();
}

void MachineEndFrame(unsigned nFrameTStates)
{
    s_nFrameBase += nFrameTStates;
}

u64 MachineTime(void)
{
    return s_nFrameBase + tstates;
}

unsigned MachineRamEnd(void)
{
    return machine_expansion ? 0x10000 : RamGetEnd();
}

// Swap a 1K page in mem[] (both of its copies) with the one kept aside
static void swap_video_page(unsigned nAddr, u8 *pOther)
{
    u8 Buffer[VIDEO_PAGE_SIZE];
    memcpy(Buffer, mem + nAddr, VIDEO_PAGE_SIZE);
    memcpy(mem + nAddr, pOther, VIDEO_PAGE_SIZE);
    memcpy(mem + nAddr + VIDEO_PAGE_SIZE, pOther, VIDEO_PAGE_SIZE);
    memcpy(pOther, Buffer, VIDEO_PAGE_SIZE);
}

// Put memory page nPage in mem[] 8000h - FFFFh, keeping the one there
static void select_memory_page(unsigned nPage)
{
    if (nPage == s_nMemoryPage) {
        return;
    }
    memcpy(s_Pages[s_nMemoryPage], mem + PAGE_START,
           s_nMemoryPage == ROM_PAGE ? ROM_PAGE_RAM : PAGE_SIZE);
    memcpy(mem + PAGE_START, s_Pages[nPage], PAGE_SIZE);
    memattr[6] = memattr[7] = nPage != ROM_PAGE;
    s_nMemoryPage = nPage;
}

// The sound chip's ports changed (a write to register 7, 14 or 15)
static void apply_ports(void)
{
    s_ucPortA = (s_AYRegs[AY_ENABLE] & 0x40) ? s_AYRegs[AY_PORT_A] : 0xFF;
    s_ucPortB = (s_AYRegs[AY_ENABLE] & 0x80) ? s_AYRegs[AY_PORT_B] : 0xFF;
    machine_attr_colour = s_ucPortA;

    unsigned nScreen = (s_ucPortB & PORT_B_SCREEN) ? 1 : 0;
    if (nScreen != s_nScreenPage) {
        swap_video_page(0x2000, s_ScreenOther);
        s_nScreenPage = nScreen;
        machine_attr = s_Attributes[nScreen];
    }
    unsigned nCharset = (s_ucPortB & PORT_B_CHARSET) ? 1 : 0;
    if (nCharset != s_nCharsetPage) {
        swap_video_page(0x2800, s_CharsetOther);
        s_nCharsetPage = nCharset;
    }
    select_memory_page(s_ucPortB & PORT_B_MEMORY);
}

// What a register reads: the unused bits are 0, a port set as input reads its pins
static u8 ay_read(unsigned nRegister)
{
    static const u8 Mask[16] = {
        0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
        0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF
    };
    if (nRegister == AY_PORT_A) {
        return s_ucPortA;
    }
    if (nRegister == AY_PORT_B) {
        return s_ucPortB;
    }
    return s_AYRegs[nRegister] & Mask[nRegister];
}

static boolean is_psg_port(u16 nPort)
{
    return (nPort & 0xF1) == 0xF1;          // 1111xxx1b
}

static boolean is_uart_port(u16 nPort)
{
    return (nPort & 0xF1) == 0xE1;          // 1110xxx1b
}

u8 MachineIn(u16 nPort)
{
    if (!machine_expansion) {
        return 0xFF;
    }
    if (is_psg_port(nPort)) {
        // BC1 = A1: reads with A1 set (FFh), nothing otherwise
        if ((nPort & 0x02) && s_nAYAddress < 16) {
            return ay_read(s_nAYAddress);
        }
        return 0xFF;
    }
    if (is_uart_port(nPort)) {
        return UartRead((nPort >> 1) & 7);
    }
    return 0xFF;
}

void MachineOut(u16 nPort, u8 ucValue)
{
    if (!machine_expansion) {
        return;
    }
    if (is_psg_port(nPort)) {
        if (!(nPort & 0x02)) {
            s_nAYAddress = ucValue;             // FDh: latch the address (0 - 15)
            return;
        }
        if (s_nAYAddress >= 16) {
            return;                             // another chip's address
        }
        s_AYRegs[s_nAYAddress] = ucValue;
        if (s_pAYWrite != nullptr) {
            s_pAYWrite(s_nAYAddress, ucValue, tstates);
        }
        if (s_nAYAddress == AY_ENABLE || s_nAYAddress == AY_PORT_A
            || s_nAYAddress == AY_PORT_B) {
            apply_ports();
        }
        return;
    }
    if (is_uart_port(nPort)) {
        UartWrite((nPort >> 1) & 7, ucValue);
    }
}

void MachineSetAYHandlers(TAYWriteHandler *pWrite, TAYResetHandler *pReset)
{
    s_pAYWrite = pWrite;
    s_pAYReset = pReset;
}

const u8 *MachineAttributes(void)
{
    return machine_attr;
}

unsigned MachineBorderColour(void)
{
    return s_ucPortB >> 4;
}

// The colour board (colour.sch) adds each colour line and the intensity line through
// equal resistors: a channel is off, half on (its colour or intensity alone) or on
u32 MachineColourRGB(unsigned nColour)
{
    static const u32 Level[3] = {0x00, 0x80, 0xFF};
    unsigned nIntensity = (nColour >> 3) & 1;
    u32 nRed   = Level[((nColour >> 2) & 1) + nIntensity];
    u32 nGreen = Level[((nColour >> 1) & 1) + nIntensity];
    u32 nBlue  = Level[(nColour & 1) + nIntensity];
    return (nRed << 16) | (nGreen << 8) | nBlue;
}

unsigned MachineScreenPage(void)
{
    return s_nScreenPage;
}

unsigned MachineCharsetPage(void)
{
    return s_nCharsetPage;
}

unsigned MachineMemoryPage(void)
{
    return s_nMemoryPage;
}

const u8 *MachineCharset(unsigned nPage)
{
    return nPage == s_nCharsetPage ? mem + 0x2C00 : s_CharsetOther;
}

void MachineSelectMemoryPage(unsigned nPage)
{
    if (!machine_expansion) {
        return;
    }
    // As OUT (FDh),15 / OUT (FFh),value with port B made an output
    s_AYRegs[AY_PORT_B] = (s_ucPortB & ~PORT_B_MEMORY) | (nPage & PORT_B_MEMORY);
    s_AYRegs[AY_ENABLE] |= 0x80;
    if (s_pAYWrite != nullptr) {
        s_pAYWrite(AY_PORT_B, s_AYRegs[AY_PORT_B], tstates);
        s_pAYWrite(AY_ENABLE, s_AYRegs[AY_ENABLE], tstates);
    }
    apply_ports();
}

/*
 * The state, MACHINE_STATE_SIZE bytes:
 *     16    the sound chip's registers as written
 *     1     the register selected
 *     2     0 (spare)
 *     1024  the screen page not shown
 *     1024  the character set page not shown
 *     2048  the attribute RAM: screen page 0, then 1
 *     4 x 32K  memory pages 0 - 3 (8000h - FFFFh; page 3's ROM half is ignored)
 */
const u8 *MachineGetState(void)
{
    if (s_pState == nullptr) {
        s_pState = new u8[MACHINE_STATE_SIZE];
        if (s_pState == nullptr) {
            return nullptr;
        }
    }
    u8 *p = s_pState;
    memcpy(p, s_AYRegs, 16);
    p += 16;
    *p++ = s_nAYAddress;
    *p++ = 0;
    *p++ = 0;
    memcpy(p, s_ScreenOther, VIDEO_PAGE_SIZE);
    p += VIDEO_PAGE_SIZE;
    memcpy(p, s_CharsetOther, VIDEO_PAGE_SIZE);
    p += VIDEO_PAGE_SIZE;
    memcpy(p, s_Attributes, sizeof s_Attributes);
    p += sizeof s_Attributes;
    for (unsigned nPage = 0; nPage < 4; nPage++, p += PAGE_SIZE) {
        memcpy(p, nPage == s_nMemoryPage ? mem + PAGE_START : s_Pages[nPage], PAGE_SIZE);
    }
    return s_pState;
}

boolean MachineSetState(const u8 *pState)
{
    if (!machine_expansion) {
        return FALSE;
    }

    const u8 *p = pState;
    const u8 *pRegs = p;
    p += 16;
    u8 nAddress = *p++;
    p += 2;

    // Take the pages in the state as they are: what it says is shown goes in mem[]
    u8 ucEnable = pRegs[AY_ENABLE];
    u8 ucPortB = (ucEnable & 0x80) ? pRegs[AY_PORT_B] : 0xFF;
    unsigned nScreen = (ucPortB & PORT_B_SCREEN) ? 1 : 0;
    unsigned nCharset = (ucPortB & PORT_B_CHARSET) ? 1 : 0;
    unsigned nMemory = ucPortB & PORT_B_MEMORY;

    // The pages shown are those the snapshot's memory image holds (already in mem[])
    memcpy(s_ScreenOther, p, VIDEO_PAGE_SIZE);
    p += VIDEO_PAGE_SIZE;
    memcpy(s_CharsetOther, p, VIDEO_PAGE_SIZE);
    p += VIDEO_PAGE_SIZE;
    memcpy(s_Attributes, p, sizeof s_Attributes);
    p += sizeof s_Attributes;
    for (unsigned nPage = 0; nPage < 4; nPage++, p += PAGE_SIZE) {
        memcpy(s_Pages[nPage], p, nPage == ROM_PAGE ? ROM_PAGE_RAM : PAGE_SIZE);
    }
    memcpy(s_Pages[ROM_PAGE] + ROM_PAGE_RAM, xrom, sizeof xrom);
    memset(s_Pages[ROM_PAGE] + ROM_PAGE_RAM + sizeof xrom, 0xFF,
           PAGE_SIZE - ROM_PAGE_RAM - sizeof xrom);
    memcpy(mem + PAGE_START, s_Pages[nMemory], PAGE_SIZE);
    memattr[6] = memattr[7] = nMemory != ROM_PAGE;

    s_nScreenPage = nScreen;
    s_nCharsetPage = nCharset;
    s_nMemoryPage = nMemory;
    machine_attr = s_Attributes[nScreen];

    // The sound chip: every register, as if written now
    if (s_pAYReset != nullptr) {
        s_pAYReset();
    }
    for (unsigned nRegister = 0; nRegister < 16; nRegister++) {
        s_AYRegs[nRegister] = pRegs[nRegister];
        if (s_pAYWrite != nullptr && nRegister != 13) {     // no envelope restart
            s_pAYWrite(nRegister, pRegs[nRegister], tstates);
        }
    }
    s_nAYAddress = nAddress;
    apply_ports();

    return TRUE;
}

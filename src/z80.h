/* Miscellaneous definitions for xz80, copyright (C) 1994 Ian Collier.
 * Modified for the Jupiter II emulator (memory writes, machine hooks).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define Z80_quit  1
#define Z80_NMI   2
#define Z80_reset 3
#define Z80_load  4
#define Z80_save  5
#define Z80_log   6

extern unsigned char mem[65536];
extern unsigned char *memptr[];
extern int memattr[];
extern int hsize,vsize;
extern int reset_ace;

/* CPU registers from an .ace snapshot, taken by mainloop() when load_snapshot is set
 * (memory is loaded before) */
struct z80_snapshot {
  unsigned short reg_af, reg_bc, reg_de, reg_hl, reg_ix, reg_iy, reg_sp, reg_pc;
  unsigned short reg_af1, reg_bc1, reg_de1, reg_hl1;
  unsigned char reg_im, reg_iff1, reg_iff2, reg_i, reg_r;   /* reg_im: 0, 1 or 2 as in IM n */
  int set_sp;
};
extern struct z80_snapshot snapshot;
extern int load_snapshot;

/* The CPU registers as they are now, for saving an .ace snapshot (F7): updated by
 * mainloop() just before every fix_tstates(), so while the Ace is paused in one */
extern struct z80_snapshot cpu_state;

/* The 96K RAM pack (ram_ace.h): LD R,A sets ram_latch_pending (with the pack), and
 * the next refresh (opcode fetch or interrupt acknowledge) calls ram_latch() */
extern int ram_paging;
extern int ram_latch_pending;
void ram_latch(unsigned char ucR);

extern unsigned int in(int h, int l);
extern unsigned int out(int h,int l, int a);
extern void do_interrupt(void);
extern void mainloop(void);
extern void fix_tstates(void);
extern void spool_error_p(unsigned char error);   /* ED FB trap, spool_ace.cpp */
extern void save_dictionary_out_p(unsigned char c);   /* ED F9 trap, save_dictionary.cpp */

/* tape_load_p() (ED FC trap, tape_sd.cpp): give the ROM's ERROR 10, or BREAK */
#define TAPE_LOAD_ERROR (-1)
#define TAPE_LOAD_BREAK (-2)
#define ROM_TAPE_ERROR  0x1ab6     /* RST 20h / DEFB 10 in the ROM's LOAD */
#define ROM_TAPE_BREAK  0x04f0     /* RST 20h / DEFB 3, where 18A7h goes on BREAK */
#define ROM_LOAD_HEADER_RETURN 0x1a81   /* after LOAD's CALL 18A7h for a header */

#define fetch(x) (memptr[(unsigned short)(x&0xe000)>>13][(x)&0x1fff])
#define fetch2(x) ((fetch((x)+1)<<8)|fetch(x))

/* The machine (machine.h): the Jupiter II main board has 2K of RAM at 3000h seen
 * twice instead of the Ace's 1K seen four times, and with the Expansion board every
 * write to the screen RAM also writes the colour attribute RAM at that cell, with
 * the PAPER and INK colours on the sound chip's port A at the time */
extern int machine_j2;
extern int machine_expansion;
extern unsigned char machine_attr_colour;
extern unsigned char *machine_attr;

/* A write: ignored where memattr[] has no RAM (ROM, missing RAM); in 2000h - 3FFFh
 * the copies the address decoding makes are written too */
static inline void
store_byte(unsigned short ad, unsigned char b)
{
  unsigned char bank=ad>>13;
  if (!memattr[bank])
    return;
  memptr[bank][ad&0x1fff]=b;
  if (bank!=1)
    return;
  if (ad<0x3000) {
    mem[ad^0x0400]=b;                   /* 2000h/2400h, 2800h/2C00h */
    if (ad<0x2800 && machine_expansion)
      machine_attr[ad&0x03ff]=machine_attr_colour;
  } else if (machine_j2) {
    mem[ad^0x0800]=b;                   /* 3000h/3800h */
  } else {
    unsigned short off=ad&0x03ff;
    mem[0x3000+off]=b;
    mem[0x3400+off]=b;
    mem[0x3800+off]=b;
    mem[0x3c00+off]=b;
  }
}

static inline void
store2func(unsigned short ad, unsigned char hi, unsigned char lo)
{
  store_byte(ad,lo);
  store_byte((unsigned short)(ad+1),hi);
}

#define storefunc(x,y) store_byte((unsigned short)(x),(unsigned char)(y))
#define store(x,y) storefunc(x,y)
#define store2b(x,hi,lo) store2func((unsigned short)(x),(unsigned char)(hi),(unsigned char)(lo))
#define store2(x,y) store2b(x,(y)>>8,(y)&255)

#define bc ((b<<8)|c)
#define de ((d<<8)|e)
#define hl ((h<<8)|l)

## 0.1 (5th November 2026)

First version of Jupiter-IIb, an emulator of the Jupiter II and its Expansion board for the Raspberry Pi.

 * The Jupiter II: 60 Hz video timing (262 lines, 54,496 t-states per frame, vsync for 4
   lines), 2K of program RAM at 0x3000 seen twice, two pages of screen and character set
   RAM, and the 16K boot ROM (the Ace ROM, patched to start the Expansion ROM).
 * The Jupiter II Expansion, the default machine: its ROM (start-up screen, extra Forth
   words), 16 colours (INK / PAPER per character cell, BORDER), the AY-3-8910 sound chip
   (stereo), 128K of RAM in four memory pages, screen and character set pages
   (`MPAGE`, `SCREEN`, `FONT`), and the 16C450 serial port (`BAUD`, `TX`, `RX`) on GPIO 4 / 5
   of the Raspberry Pi 4 / 400.
 * Ctrl+F12 chooses the machine: Jupiter II + Expansion, Jupiter II or Jupiter Ace, the
   last two with a RAM pack.
 * `.ace` snapshots of the Jupiter II with the Expansion keep its whole state (pages, colours,
   sound chip) in a block after the end marker. The 96K RAM pack's block now starts with
   `ACE-96K`.
 * Licensed under the GNU GPL version 3, with the parts from other projects under their
   own licences (see the README's *Licence*).

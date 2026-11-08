# Review of `04_game_consoles_and_amiga` (2026-11-07)

| # | Finding | Fix / status |
| :-- | :-- | :-- |
| 1 | MARIA renderer treated 5-byte indirect headers as direct (plan said "exact direct and indirect parser") | Indirect (character) mode with CHARBASE and 2-byte characters implemented; 160B, 320 modes, holey DMA, Kangaroo mode and the DLL walk stated as missing; "cycle-exact" removed from the plan. |
| 2 | GTIA overlay had players only (plan said 4 players + 4 missiles) and no SIZEP/SIZEM | Missiles and widths added; priority order documented. |
| 3 | Plan: pico-mac "runs a Mac 128K/384K/512K in SRAM" | pico-mac documents `MEMSIZE` up to about 208 KB; corrected, with the note that its video path is not HSTX. |
| 4 | Plan: repository names | All verified: PicoPlus-devel is the current home of the pico-*Plus emulators (fhoedemakers redirects there), harbaum/NanoMig redirects to MiSTle-Dev, Pico2Atari8 is under alijani1. `pico_shared/BoardConfigs.cmake` shows `HW_CONFIG 6` is a PIO-DVI pinout on GPIO 6-13; the HSTX configuration for the 12-19 wiring is `HW_CONFIG 2`. |
| 5 | Plan: Tang Primer 25K core board 22.9 x 17.8 mm | Sipeed gives 23 x 18 mm; fine. |
| 6 | ColecoVision keypad table and Macintosh renderer | Checked against the usual tables; correct, kept. |
| 7 | Header syntax | Compiles clean on the host with `-Wall -Wextra`; an ANTIC playfield renderer and the DLL walker remain to be written before any of this shows a game. |

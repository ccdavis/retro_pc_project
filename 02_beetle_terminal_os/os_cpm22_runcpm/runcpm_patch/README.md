# Running real CP/M 2.2 on the Beetle: RunCPM + the AirTag host extensions

`beetle_cpm22_ssh.ino` in the parent directory is **not** a CP/M emulator: it is a
CP/M-flavoured host shell that executes only the four generated `.COM` utilities.
To run WordStar, Turbo Pascal, MBASIC and the rest you need a Z80 and a BDOS, and
[MockbaTheBorg/RunCPM](https://github.com/MockbaTheBorg/RunCPM) (MIT) already runs
on the ESP32.  These files add our BDOS functions to it:

| File | What |
| :-- | :-- |
| `runcpm_cpm_h.patch` | adds cases 220..224 to RunCPM's `_Bdos()` switch and includes the header below (applies to RunCPM commit 12fc57a; two hunks) |
| `runcpm_host_bdos.h` | `host_bdos_dispatch()`: copies the CP/M command tail (or, for 224, the NUL-terminated text) out of Z80 RAM and calls `host_bdos_ssh/telnet/wifi/bank/speak(const char *)`; weak defaults print a message so the posix build still links |

**Verified 2026-11-07** on the Linux build (`make -f Makefile.posix CCP=INTERNAL` in `tools/RunCPM/RunCPM`,
`disk_A/*.COM` and project 5's `SPEAK.COM` in `A/0/`): the internal CCP loads each utility,
`SSH` alone prints its usage line, `SSH pi@host 2222` reaches the hook with `PI@HOST 2222`
(the CCP upper-cases the tail), `TELNET`, `WIFI` and `BANK 3` likewise, and the utilities
return to the prompt through the warm boot.

Steps:

1. `git clone https://github.com/MockbaTheBorg/RunCPM && cd RunCPM && git apply <this dir>/runcpm_cpm_h.patch && cp <this dir>/runcpm_host_bdos.h RunCPM/`
2. Host check: `make -C RunCPM -f Makefile.posix CCP=INTERNAL` (RunCPM's posix build defines no CCP by itself), create `A/0/` next to the binary with the `.COM` files, run `./RunCPM`; `A0>SSH` prints the usage line, `A0>SSH pi@host` prints "SSH: no host network on this build: PI@HOST".
3. Beetle build: open `RunCPM/RunCPM.ino` in the Arduino IDE for the Beetle ESP32-C6, provide a filesystem abstraction (RunCPM ships `abstraction_arduino.h` for SdFat; the Beetle has no SD slot, so a LittleFS variant with the same `_sys_*` functions is needed: same `fs::File` API, `LittleFS.open(path, "r"/"w"/"a")`), and implement the four `host_bdos_*` functions with the code from `beetle_cpm22_ssh.ino` (`cpm_bdos_ssh_handler` etc. take the tail string).
4. The 16 KB bank window at 0x8000 (`OUT (0F0h),A`) hooks into `cpu_out()` in RunCPM's `cpu3.h`; RunCPM's own `BANKS` mechanism banks the whole TPA instead and is for CP/M 3.

Memory: RunCPM needs 64 KB for the Z80 plus its own ~20 KB; with Wi-Fi, BLE and
LibSSH-ESP32 resident that fits the ESP32-C6 (512 KB) with room for a 32-64 KB RAM
disk, not the 128 KB the first plan assumed, and not on the ESP32-C3 (400 KB) with
SSH active.

#!/usr/bin/env python3
# Run with: uv run make_cpm_tools.py   (standard library only)
"""
Generates real Z80 CP/M 2.2 transient binaries (SSH.COM, TELNET.COM, WIFI.COM, BANK.COM)
for the AirTag Beetle CP/M 2.2 environment (or an external RunCPM SD/LittleFS image).

Each .COM file loads at 0x0100 in the Z80 Transient Program Area (TPA):
  1. Checks if the user passed any arguments in the CP/M command tail at 0x0080.
     If empty (for SSH.COM / TELNET.COM), prints a $-terminated usage banner
     using standard CP/M BDOS Function 9 (C=09h, CALL 0005h).
  2. Otherwise loads DE = 0x0080 and invokes the ESP32 Host Network/MMU BDOS trap:
       - BDOS 220 (0xDC): SSH.COM    (LibSSH-ESP32 VT100 client)
       - BDOS 221 (0xDD): TELNET.COM (TCP Telnet / BBS client)
       - BDOS 222 (0xDE): WIFI.COM   (ESP32 Wi-Fi connection manager)
       - BDOS 223 (0xDF): BANK.COM   (128 KB MMU Bank Switcher & RAM Disk status)
  3. Returns to the CP/M 'A>' prompt via JP 0000h (Warm Boot).

Verified under z80pack cpmsim (CP/M 2.2): the usage text prints cleanly; the host
BDOS calls are no-ops there (a real CP/M ignores unknown function numbers).
"""

import os


def build_cpm_com_binary(bdos_func: int, usage_msg: str, allow_empty_args: bool = False) -> bytes:
    code = bytearray()

    # The usage message follows the code; its address is computed from the real
    # code length (the first version hard-coded 0x0116 for a 30-byte prologue and
    # printed eight bytes of machine code before the text).
    prologue_len = (3 + 1 + 2 + 3 + 2 + 3 + 5) if not allow_empty_args else 0
    msg_addr = 0x0100 + prologue_len + 3 + 2 + 3 + 3

    if not allow_empty_args:
        # 0100: LD A, (0080h)   ; Read command tail byte count
        code += bytes([0x3A, 0x80, 0x00])
        # 0103: OR A            ; Is length == 0?
        code += bytes([0xB7])
        # 0104: JR NZ, call_host (+8 bytes -> 0x010E)
        code += bytes([0x20, 0x08])
        # 0106: LD DE, msg_addr  ; usage text, after the code
        code += bytes([0x11, msg_addr & 0xFF, (msg_addr >> 8) & 0xFF])
        # 0109: LD C, 09h       ; Standard CP/M BDOS Print String
        code += bytes([0x0E, 0x09])
        # 010B: CALL 0005h
        code += bytes([0xCD, 0x05, 0x00])
        # 010E (call_host): LD A,(0080h); OR A; RET Z  -> back to the CCP when there is no tail
        code += bytes([0x3A, 0x80, 0x00, 0xB7, 0xC8])

    # LD DE, 0080h              ; Point DE to CP/M command tail
    code += bytes([0x11, 0x80, 0x00])
    # LD C, bdos_func           ; Custom ESP32 Host Bridge BDOS function
    code += bytes([0x0E, bdos_func])
    # CALL 0005h                ; Trap into ESP32 host
    code += bytes([0xCD, 0x05, 0x00])
    # JP 0000h                  ; Warm boot to A>
    code += bytes([0xC3, 0x00, 0x00])
    assert len(code) == msg_addr - 0x0100, "usage message address out of step with the code"

    # Append $-terminated CP/M usage string
    code += usage_msg.encode("ascii") + b"\r\n$"

    # Pad to a multiple of 128 bytes (standard CP/M sector record size) with 0x1A (EOF)
    rem = len(code) % 128
    if rem != 0:
        code += bytes([0x1A] * (128 - rem))
    return bytes(code)


def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "disk_A")
    os.makedirs(out_dir, exist_ok=True)

    targets = [
        ("SSH.COM",    220, "Usage: A>SSH user@host [port]", False),
        ("TELNET.COM", 221, "Usage: A>TELNET host [port]",   False),
        ("WIFI.COM",   222, "Usage: A>WIFI [ssid] [pass]",   True),
        ("BANK.COM",   223, "Usage: A>BANK [0..7]",          True),
    ]

    for filename, bdos_num, usage, allow_empty in targets:
        blob = build_cpm_com_binary(bdos_num, usage, allow_empty)
        path = os.path.join(out_dir, filename)
        with open(path, "wb") as f:
            f.write(blob)
        print(f"Generated {path} ({len(blob)} bytes, BDOS {bdos_num})")


if __name__ == "__main__":
    main()

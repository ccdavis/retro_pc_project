/**
 * AirTag Retro Terminal - OS Option B: CP/M 2.2 Host BDOS Extension,
 * 128 KB Silicon RAM Disk (Drive B:/M:), and 16 KB MMU Bank Switcher
 *
 * 1. Network & Memory Host BDOS Extensions (via CALL 0005h):
 *      - BDOS C = 220 (0xDC): Host SSH-2 Client  (SSH.COM)
 *      - BDOS C = 221 (0xDD): Host Telnet Client (TELNET.COM)
 *      - BDOS C = 222 (0xDE): Host Wi-Fi Manager (WIFI.COM)
 *      - BDOS C = 223 (0xDF): MMU 16KB Bank Select / Status (BANK.COM)
 *
 * 2. Z80 Hardware MMU I/O Port (0xF0):
 *      Executing `LD A, bank` -> `OUT (0F0h), A` in Z80 code swaps any of the
 *      8 x 16 KB Expanded SRAM Banks (128 KB total) into the Z80 address window
 *      at 0x8000..0xBFFF.
 *
 * 3. 128 KB Silicon RAM Disk (CP/M Drive B: / Drive M:):
 *      Gives unmodified out-of-the-box CP/M 2.2 software (WordStar .OVR overlays,
 *      dBASE II, multi-pass compilers) instant microsecond access to the Beetle's
 *      extra 128 KB of SRAM with zero flash wear.
 */

#ifndef SSH_COM_BDOS_EXT_H
#define SSH_COM_BDOS_EXT_H

#include <Arduino.h>
#include <LittleFS.h>

#define BDOS_HOST_SSH     220  // 0xDC
#define BDOS_HOST_TELNET  221  // 0xDD
#define BDOS_HOST_WIFI    222  // 0xDE
#define BDOS_HOST_BANK    223  // 0xDF

#define MMU_IO_PORT       0xF0 // Z80 OUT (0F0h), A selects 16KB bank at 0x8000..0xBFFF
#ifndef MMU_NUM_BANKS
#define MMU_NUM_BANKS     4    // 16 KB banks; the sketch sets this per chip (C6: 4 = 64 KB, C3: 1)
#endif
#define MMU_BANK_SIZE     16384

// ============================================================================
// 128 KB EXPANDED SRAM POOL (Shared by 0x8000 MMU Window & Drive B:/M: RAM Disk)
// ============================================================================
static uint8_t s_expandedSram[MMU_NUM_BANKS][MMU_BANK_SIZE];
static uint8_t s_activeMmuBank = 0;

/**
 * Swaps the active 16 KB window at Z80 0x8000..0xBFFF with s_expandedSram[newBank]
 */
static inline void cpm_mmu_select_bank(uint8_t* z80_ram, uint8_t newBank) {
    newBank %= MMU_NUM_BANKS;
    if (newBank == s_activeMmuBank) return;

    // Save current 0x8000..0xBFFF window back into its bank slot
    memcpy(s_expandedSram[s_activeMmuBank], &z80_ram[0x8000], MMU_BANK_SIZE);
    // Map new bank into 0x8000..0xBFFF
    memcpy(&z80_ram[0x8000], s_expandedSram[newBank], MMU_BANK_SIZE);
    s_activeMmuBank = newBank;
}

// ============================================================================
// SILICON RAM DISK (CP/M DRIVE B: / DRIVE M:) BACKED BY EXPANDED SRAM
// ============================================================================
#define RAMDISK_MAX_FILES  16

struct RamDiskEntry {
    bool     used;
    char     name[13]; // "FILENAME.EXT"
    uint32_t offset;
    uint32_t size;
};

static RamDiskEntry s_ramDiskDir[RAMDISK_MAX_FILES];
static uint32_t     s_ramDiskWritePtr = MMU_BANK_SIZE; // bank 0 is the default window; banks 1.. hold the RAM disk files

static inline void ramdisk_format() {
    memset(s_ramDiskDir, 0, sizeof(s_ramDiskDir));
    s_ramDiskWritePtr = MMU_BANK_SIZE;
}

static inline int ramdisk_find_file(const String& fname) {
    String up = fname;
    up.trim();
    up.toUpperCase();
    for (int i = 0; i < RAMDISK_MAX_FILES; i++) {
        if (s_ramDiskDir[i].used && up.equals(s_ramDiskDir[i].name)) {
            return i;
        }
    }
    return -1;
}

static inline bool ramdisk_write_file(const String& fname, const uint8_t* data, uint32_t len) {
    String up = fname;
    up.trim();
    up.toUpperCase();

    int existing = ramdisk_find_file(up);
    if (existing >= 0 && len <= s_ramDiskDir[existing].size) {
        uint8_t* poolBase = &s_expandedSram[0][0];
        memcpy(poolBase + s_ramDiskDir[existing].offset, data, len);
        s_ramDiskDir[existing].size = len;
        return true;
    }

    uint32_t totalPoolBytes = MMU_NUM_BANKS * MMU_BANK_SIZE;
    if (s_ramDiskWritePtr + len > totalPoolBytes) {
        return false; // RAM Disk full
    }

    int slot = (existing >= 0) ? existing : -1;
    if (slot < 0) {
        for (int i = 0; i < RAMDISK_MAX_FILES; i++) {
            if (!s_ramDiskDir[i].used) { slot = i; break; }
        }
    }
    if (slot < 0) return false;

    uint8_t* poolBase = &s_expandedSram[0][0];
    memcpy(poolBase + s_ramDiskWritePtr, data, len);

    s_ramDiskDir[slot].used = true;
    strncpy(s_ramDiskDir[slot].name, up.c_str(), 12);
    s_ramDiskDir[slot].name[12] = '\0';
    s_ramDiskDir[slot].offset = s_ramDiskWritePtr;
    s_ramDiskDir[slot].size   = len;
    s_ramDiskWritePtr += len;
    return true;
}

static inline int32_t ramdisk_read_file(const String& fname, uint8_t* destBuf, uint32_t maxLen) {
    int slot = ramdisk_find_file(fname);
    if (slot < 0) return -1;
    uint32_t toCopy = min(s_ramDiskDir[slot].size, maxLen);
    const uint8_t* poolBase = &s_expandedSram[0][0];
    memcpy(destBuf, poolBase + s_ramDiskDir[slot].offset, toCopy);
    return (int32_t)toCopy;
}

static inline bool ramdisk_delete_file(const String& fname) {
    int slot = ramdisk_find_file(fname);
    if (slot < 0) return false;
    s_ramDiskDir[slot].used = false;
    return true;
}

static inline uint32_t ramdisk_free_bytes() {
    uint32_t totalPoolBytes = MMU_NUM_BANKS * MMU_BANK_SIZE;
    return (totalPoolBytes > s_ramDiskWritePtr) ? (totalPoolBytes - s_ramDiskWritePtr) : 0;
}

// ============================================================================
// Z80 MACHINE CODE IMAGES FOR SSH.COM, TELNET.COM, WIFI.COM, BANK.COM (@0x0100)
//
// These are the minimal images (no usage text); make_cpm_tools.py generates the
// fuller ones in disk_A/ and is the reference.  Disassembly:
//   0100: 11 80 00    LD   DE, 0080h   ; Point DE to CP/M command tail buffer
//   0103: 0E <BDOS>   LD   C, <220..223>
//   0105: CD 05 00    CALL 0005h       ; Trap into ESP32 BDOS handler
//   0108: C3 00 00    JP   0000h       ; Warm boot back to CP/M prompt
// ============================================================================
static const uint8_t Z80_SSH_COM_BIN[128] = {
    0x11, 0x80, 0x00, 0x0E, BDOS_HOST_SSH,    0xCD, 0x05, 0x00, 0xC3, 0x00, 0x00, 0x1A
};

static const uint8_t Z80_TELNET_COM_BIN[128] = {
    0x11, 0x80, 0x00, 0x0E, BDOS_HOST_TELNET, 0xCD, 0x05, 0x00, 0xC3, 0x00, 0x00, 0x1A
};

static const uint8_t Z80_WIFI_COM_BIN[128] = {
    0x11, 0x80, 0x00, 0x0E, BDOS_HOST_WIFI,   0xCD, 0x05, 0x00, 0xC3, 0x00, 0x00, 0x1A
};

static const uint8_t Z80_BANK_COM_BIN[128] = {
    0x11, 0x80, 0x00, 0x0E, BDOS_HOST_BANK,   0xCD, 0x05, 0x00, 0xC3, 0x00, 0x00, 0x1A
};

/**
 * Extracts the CP/M command-line tail string from Z80 RAM at 0x0080..0x00FF.
 */
static inline String cpm_read_cmd_tail(const uint8_t* z80_ram, uint16_t dma_addr = 0x0080, bool forceLower = false) {
    uint8_t len = z80_ram[dma_addr];
    if (len > 126) len = 126;

    String out = "";
    for (uint8_t i = 0; i < len; i++) {
        char c = (char)z80_ram[dma_addr + 1 + i];
        if (c == 0 || c == '\r' || c == '\n') break;
        out += c;
    }
    out.trim();
    if (forceLower) {
        out.toLowerCase();
    }
    return out;
}

/**
 * Auto-provisions /A0/SSH.COM, /A0/TELNET.COM, /A0/WIFI.COM, and /A0/BANK.COM
 * onto the Beetle's 2 MB LittleFS flash (Drive A:) AND pre-loads them into the
 * 128 KB Silicon RAM Disk (Drive B: / M:).
 */
static inline void cpm_provision_network_com_files() {
    ramdisk_format();
    if (!LittleFS.exists("/A0")) {
        LittleFS.mkdir("/A0");
    }
    struct ComImage { const char* path; const char* name; const uint8_t* bin; };
    const ComImage imgs[] = {
        {"/A0/SSH.COM",    "SSH.COM",    Z80_SSH_COM_BIN},
        {"/A0/TELNET.COM", "TELNET.COM", Z80_TELNET_COM_BIN},
        {"/A0/WIFI.COM",   "WIFI.COM",   Z80_WIFI_COM_BIN},
        {"/A0/BANK.COM",   "BANK.COM",   Z80_BANK_COM_BIN},
    };
    for (const auto& img : imgs) {
        if (!LittleFS.exists(img.path)) {
            File f = LittleFS.open(img.path, "w");
            if (f) { f.write(img.bin, 128); f.close(); }
        }
        ramdisk_write_file(img.name, img.bin, 128);
    }
}

#endif // SSH_COM_BDOS_EXT_H

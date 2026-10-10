#pragma once

// Parser for the 688-byte rds_t packets that the MTK FM driver delivers via
// read(/dev/fm). Layout verified against the kernel source
// (connectivity/fmradio/inc/fm_rds.h) and the on-device library. Pure logic;
// host-tested.

#include <stddef.h>
#include <stdint.h>

constexpr size_t kMtkRdsPacketSize = 688;

// rds_t field offsets (see .agents/mtk-fm-port-research.md §5.1).
constexpr size_t kMtkRdsOffStereoFlag = 15;   // rds_flag_t.Stereo
constexpr size_t kMtkRdsOffPi = 24;           // u16
constexpr size_t kMtkRdsOffPty = 27;          // u8
constexpr size_t kMtkRdsOffAfNum = 28;        // s16
constexpr size_t kMtkRdsOffAfList = 30;       // s16[2][25], 100kHz units
constexpr size_t kMtkRdsOffEcc = 244;         // u8
constexpr size_t kMtkRdsOffPs = 248;          // u8[4][8]; window 0 is current
constexpr size_t kMtkRdsOffRtText = 290;      // u8[4][64]; window 0 is current
constexpr size_t kMtkRdsOffRtLength = 548;    // u8 TextLength
constexpr size_t kMtkRdsOffEventStatus = 554; // u16

// event_status bits (kernel fm_rds.h).
constexpr uint16_t kMtkRdsEventFlags = 0x0001;
constexpr uint16_t kMtkRdsEventPi = 0x0002;
constexpr uint16_t kMtkRdsEventPty = 0x0004;
constexpr uint16_t kMtkRdsEventPs = 0x0008;
constexpr uint16_t kMtkRdsEventRt = 0x0040;
constexpr uint16_t kMtkRdsEventAfHint = 0x0080;
constexpr uint16_t kMtkRdsEventAfList = 0x0100;
constexpr uint16_t kMtkRdsEventEcc = 0x1000;
constexpr uint16_t kMtkRdsEventRdsPresent = 0x2000;
constexpr uint16_t kMtkRdsEventNoRds = 0x4000;

constexpr int kMtkRdsMaxAf = 25;

struct MtkRdsPacket {
    bool has_pi = false;
    uint16_t pi = 0;
    bool has_pty = false;
    uint8_t pty = 0;
    bool has_ps = false;
    char ps[9] = "";
    bool has_rt = false;
    char rt[65] = "";
    bool has_ecc = false;
    uint8_t ecc = 0;
    bool has_stereo = false;
    bool stereo = false;
    bool has_af = false;
    int af_count = 0;
    int af_khz[kMtkRdsMaxAf] = {};
    bool af_switch_hint = false;
};

// Parses one packet. Returns false for null/wrong-sized input; out is
// always fully reset first. Only fields whose event bits are set are
// produced (has_* flags).
bool mtk_rds_parse(const uint8_t *buf, size_t len, MtkRdsPacket *out);

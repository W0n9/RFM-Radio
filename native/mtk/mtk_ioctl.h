#pragma once

// MediaTek FM driver (fmradio_drv_mt6631_6635) userspace ABI.
// Ioctl numbers and layouts come from the public kernel header
// (drivers/misc/mediatek/connectivity/fmradio/inc/fm_ioctl.h + fm_main.h)
// and were verified against the on-device .ko by disassembly and a live
// smoke test. Note the kernel encodes pointer types (size 8) for most
// commands; SETMONOSTERO is the exception (by-value int32, size 4).

#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>

struct fm_tune_parm {
    uint8_t err;         // OUT: driver result detail
    uint8_t band;        // IN: kMtkBand*
    uint8_t space;       // IN: kMtkSpace*
    uint8_t hilo;        // IN: kMtkHilo*
    uint8_t deemphasis;  // IN: kMtkDeemphasis*
    uint16_t freq;       // IN/OUT, 10kHz units (8750 = 87.5MHz)
};

static_assert(sizeof(fm_tune_parm) == 8, "fm_tune_parm must stay 8 bytes");
static_assert(offsetof(fm_tune_parm, freq) == 6, "freq must sit at offset 6");

constexpr uint8_t kMtkBandUe = 1;        // 87.5-108 MHz
constexpr uint8_t kMtkBandJapan = 2;     // 76-90 MHz
constexpr uint8_t kMtkBandJapanWide = 3; // 76-108 MHz

constexpr uint8_t kMtkSpace100k = 1;
constexpr uint8_t kMtkSpace200k = 2;
constexpr uint8_t kMtkSpace50k = 5;

constexpr uint8_t kMtkHiloOff = 0;

// Driver semantics are inverted relative to RegionProfile.emphasis:
// 0 = 50us, 1 = 75us.
constexpr uint8_t kMtkDeemphasis50us = 0;
constexpr uint8_t kMtkDeemphasis75us = 1;

// SETMONOSTERO values: 0 = auto (stereo allowed), 1 = force mono.
constexpr int32_t kMtkStereoAuto = 0;
constexpr int32_t kMtkForceMono = 1;

#define MTK_IOCTL_MAGIC 0xf5

#define MTK_IOCTL_POWERUP           _IOWR(MTK_IOCTL_MAGIC, 0, struct fm_tune_parm*)
#define MTK_IOCTL_POWERDOWN         _IOWR(MTK_IOCTL_MAGIC, 1, int32_t*)
#define MTK_IOCTL_TUNE              _IOWR(MTK_IOCTL_MAGIC, 2, struct fm_tune_parm*)
#define MTK_IOCTL_MUTE              _IOWR(MTK_IOCTL_MAGIC, 6, uint32_t*)
#define MTK_IOCTL_GETRSSI           _IOWR(MTK_IOCTL_MAGIC, 7, int32_t*)
#define MTK_IOCTL_GETCHIPID         _IOWR(MTK_IOCTL_MAGIC, 10, uint16_t*)
#define MTK_IOCTL_GETMONOSTERO      _IOWR(MTK_IOCTL_MAGIC, 13, uint16_t*)
#define MTK_IOCTL_RDS_ONOFF         _IOWR(MTK_IOCTL_MAGIC, 18, uint16_t*)
#define MTK_IOCTL_RDS_SUPPORT       _IOWR(MTK_IOCTL_MAGIC, 19, int32_t*)
#define MTK_IOCTL_IS_FM_POWERED_UP  _IOWR(MTK_IOCTL_MAGIC, 24, uint32_t*)
#define MTK_IOCTL_ANA_SWITCH        _IOWR(MTK_IOCTL_MAGIC, 30, int32_t*)
#define MTK_IOCTL_SETMONOSTERO      _IOWR(MTK_IOCTL_MAGIC, 37, int32_t)
#define MTK_IOCTL_SOFT_MUTE_TUNE    _IOWR(MTK_IOCTL_MAGIC, 63, struct fm_softmute_tune_t*)

// One blocking ioctl that tunes, samples, and judges the channel inside the
// kernel/firmware (the path MIUI's libfmjni uses for seek/scan; the driver's
// FM_IOCTL_SEEK/SCAN are dead on this .ko). Layout from kernel fm_main.h,
// natural alignment: rssi@0, freq@4-5, pad@6-7, valid@8-11.
//
// Scale caution: rssi is the raw 10-bit CQI value, NOT the GETRSSI scale.
// GETRSSI (mt6631_GetCurRSSI) applies *6/16 to produce a dBm-like value;
// SOFT_MUTE_TUNE (mt6631_soft_mute_tune) returns the folded 10-bit value
// as-is. The kernel's own RSSI threshold -296 (fm_cust.cfg) is therefore
// roughly -111 dBm, and our -88 dBm corresponds to about -235 raw.
//
// valid is the kernel's joint verdict (RSSI/PAMD/MR/ATDC/PRX/ATDEV/SMG
// against fm_cust.cfg + compiled defaults). On a failed ioctl the kernel
// writes nothing back (fm_module.c copies out only on success), so callers
// must treat a false return as "no reading", not "valid=0".
struct fm_softmute_tune_t {
    int32_t rssi;    // OUT: raw CQI RSSI (10-bit folded, ~0.375x the dBm scale)
    uint16_t freq;   // IN: 10 kHz units (8750 = 87.5 MHz)
    int32_t valid;   // OUT: kernel joint verdict, 1 = station
};

static_assert(sizeof(fm_softmute_tune_t) == 12, "fm_softmute_tune_t must stay 12 bytes");
static_assert(offsetof(fm_softmute_tune_t, freq) == 4, "freq must sit at offset 4");
static_assert(offsetof(fm_softmute_tune_t, valid) == 8, "valid must sit at offset 8");

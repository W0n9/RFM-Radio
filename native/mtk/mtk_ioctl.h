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

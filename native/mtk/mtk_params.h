#pragma once

// Pure mapping helpers between RFM protocol values and MTK driver values.
// No fd or ioctl usage here; keep this module host-testable.

#include <stdint.h>

#include "mtk_ioctl.h"

// spacing_khz (50/100/200) -> driver space value (5/1/2). Unknown values
// fall back to 100kHz, matching the legacy adapter's default-arm pattern.
int mtk_spacing_from_khz(int spacing_khz);

// RegionProfile.emphasis -> driver deemphasis. INVERTED semantics:
// profile 0 = 75us -> driver 1; profile 1 = 50us -> driver 0.
int mtk_deemphasis_from_emphasis(int emphasis);

// Protocol kHz <-> driver 10kHz units.
uint16_t mtk_freq_from_khz(uint32_t frequency_khz);
uint32_t khz_from_mtk_freq(uint16_t freq);

// Builds a fully-mapped tune/powerup parameter block. hilo is always off
// (no hardware seek on this driver family, so high/low injection is moot).
fm_tune_parm mtk_make_tune_parm(uint8_t band, int spacing_khz, int emphasis, uint32_t frequency_khz);

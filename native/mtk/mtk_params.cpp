#include "mtk_params.h"

int mtk_spacing_from_khz(int spacing_khz) {
    switch (spacing_khz) {
        case 50:
            return kMtkSpace50k;
        case 200:
            return kMtkSpace200k;
        case 100:
        default:
            return kMtkSpace100k;
    }
}

int mtk_deemphasis_from_emphasis(int emphasis) {
    // RegionProfile: 0 = 75us, 1 = 50us. Driver: 0 = 50us, 1 = 75us.
    return emphasis == 1 ? kMtkDeemphasis50us : kMtkDeemphasis75us;
}

uint16_t mtk_freq_from_khz(uint32_t frequency_khz) {
    return static_cast<uint16_t>(frequency_khz / 10);
}

uint32_t khz_from_mtk_freq(uint16_t freq) {
    return static_cast<uint32_t>(freq) * 10;
}

fm_tune_parm mtk_make_tune_parm(uint8_t band, int spacing_khz, int emphasis, uint32_t frequency_khz) {
    fm_tune_parm parm = {};
    parm.band = band;
    parm.space = static_cast<uint8_t>(mtk_spacing_from_khz(spacing_khz));
    parm.hilo = kMtkHiloOff;
    parm.deemphasis = static_cast<uint8_t>(mtk_deemphasis_from_emphasis(emphasis));
    parm.freq = mtk_freq_from_khz(frequency_khz);
    return parm;
}

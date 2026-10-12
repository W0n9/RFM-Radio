#include "region_profile.h"

#include <algorithm>

namespace {

constexpr RegionProfile kEuropeProfile = {
        .lower_frequency_khz = 87500,
        .upper_frequency_khz = 108000,
        .emphasis = kFmEmphasis50Microseconds,
        .rds_standard = kFmRdsStandardRds,
        .legacy_band = 1,
        .helium_region = 1,
        .mtk_band = 1,
};

constexpr RegionProfile kUnitedStatesProfile = {
        .lower_frequency_khz = 87500,
        .upper_frequency_khz = 108000,
        .emphasis = kFmEmphasis75Microseconds,
        .rds_standard = kFmRdsStandardRbds,
        .legacy_band = 1,
        .helium_region = 0,
        .mtk_band = 1,
};

constexpr RegionProfile kJapanProfile = {
        .lower_frequency_khz = 76000,
        .upper_frequency_khz = 95000,
        .emphasis = kFmEmphasis50Microseconds,
        .rds_standard = kFmRdsStandardRds,
        .legacy_band = 2,
        .helium_region = 2,
        // Driver band 2 spans only 76-90MHz while this region reaches 95MHz;
        // band 3 (76-108) covers the whole region, so tuning can never be
        // rejected for being out of band.
        .mtk_band = 3,
};

constexpr RegionProfile kJapanWideProfile = {
        .lower_frequency_khz = 76000,
        .upper_frequency_khz = 108000,
        .emphasis = kFmEmphasis50Microseconds,
        .rds_standard = kFmRdsStandardRds,
        .legacy_band = 3,
        .helium_region = 3,
        .mtk_band = 3,
};

}  // namespace

const RegionProfile &get_region_profile(StartupRegion region) {
    switch (region) {
        case StartupRegion::kUnitedStates:
            return kUnitedStatesProfile;

        case StartupRegion::kJapan:
            return kJapanProfile;

        case StartupRegion::kJapanWide:
            return kJapanWideProfile;

        case StartupRegion::kEurope:
        default:
            return kEuropeProfile;
    }
}

const char *get_region_name(StartupRegion region) {
    switch (region) {
        case StartupRegion::kUnitedStates:
            return "us";

        case StartupRegion::kJapan:
            return "jp";

        case StartupRegion::kJapanWide:
            return "jp_wide";

        case StartupRegion::kEurope:
        default:
            return "eu";
    }
}

uint32_t clamp_frequency_to_region(const RegionProfile &profile, uint32_t frequency_khz) {
    return std::min(std::max(frequency_khz, profile.lower_frequency_khz), profile.upper_frequency_khz);
}

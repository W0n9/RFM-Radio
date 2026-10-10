#include <gtest/gtest.h>

#include "mtk/mtk_params.h"
#include "region_profile.h"

namespace {

TEST(MtkParamsTest, MapsSpacingKhzToVendorSpace) {
    EXPECT_EQ(mtk_spacing_from_khz(50), 5);
    EXPECT_EQ(mtk_spacing_from_khz(100), 1);
    EXPECT_EQ(mtk_spacing_from_khz(200), 2);
    EXPECT_EQ(mtk_spacing_from_khz(75), 1);  // invalid values fall back to 100kHz
}

TEST(MtkParamsTest, MapsEmphasisToInvertedDeemphasis) {
    // MTK driver deemphasis semantics are inverted relative to RegionProfile.emphasis:
    // driver 0 = 50us, 1 = 75us.
    EXPECT_EQ(mtk_deemphasis_from_emphasis(kFmEmphasis50Microseconds), 0);
    EXPECT_EQ(mtk_deemphasis_from_emphasis(kFmEmphasis75Microseconds), 1);
}

TEST(MtkParamsTest, ConvertsKhzToTenKhzUnits) {
    EXPECT_EQ(mtk_freq_from_khz(87500), 8750);
    EXPECT_EQ(mtk_freq_from_khz(76000), 7600);
    EXPECT_EQ(mtk_freq_from_khz(108000), 10800);
    EXPECT_EQ(mtk_freq_from_khz(87950), 8795);
    EXPECT_EQ(khz_from_mtk_freq(8750), 87500u);
}

TEST(MtkParamsTest, PacksTuneParmFields) {
    const fm_tune_parm parm = mtk_make_tune_parm(1, 50, kFmEmphasis50Microseconds, 101700);

    EXPECT_EQ(parm.err, 0);
    EXPECT_EQ(parm.band, 1);
    EXPECT_EQ(parm.space, 5);
    EXPECT_EQ(parm.hilo, 0);
    EXPECT_EQ(parm.deemphasis, 0);
    EXPECT_EQ(parm.freq, 10170);
}

TEST(MtkParamsTest, IoctlEncodingMatchesVerifiedDeviceValues) {
    // Values verified by disassembling the on-device fmradio_drv_mt6631_6635.ko
    // and confirmed by a live smoke test (POWERUP/TUNE/GETRSSI/POWERDOWN).
    EXPECT_EQ(MTK_IOCTL_POWERUP, 0xc008f500u);
    EXPECT_EQ(MTK_IOCTL_POWERDOWN, 0xc008f501u);
    EXPECT_EQ(MTK_IOCTL_TUNE, 0xc008f502u);
    EXPECT_EQ(MTK_IOCTL_MUTE, 0xc008f506u);
    EXPECT_EQ(MTK_IOCTL_GETRSSI, 0xc008f507u);
    EXPECT_EQ(MTK_IOCTL_GETCHIPID, 0xc008f50au);
    EXPECT_EQ(MTK_IOCTL_GETMONOSTERO, 0xc008f50du);
    EXPECT_EQ(MTK_IOCTL_RDS_ONOFF, 0xc008f512u);
    EXPECT_EQ(MTK_IOCTL_RDS_SUPPORT, 0xc008f513u);
    EXPECT_EQ(MTK_IOCTL_IS_FM_POWERED_UP, 0xc008f518u);
    EXPECT_EQ(MTK_IOCTL_ANA_SWITCH, 0xc008f51eu);
    EXPECT_EQ(MTK_IOCTL_SETMONOSTERO, 0xc004f525u);
}

}  // namespace

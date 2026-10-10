#include <gtest/gtest.h>

#include <string.h>

#include "mtk/rds_packet.h"

namespace {

constexpr size_t kPacketSize = 688;

// Builds a zeroed rds_t with the given event bits set.
void make_packet(uint8_t *buf, uint16_t event_status) {
    memset(buf, 0, kPacketSize);
    uint16_t *es = reinterpret_cast<uint16_t *>(buf + 554);
    *es = event_status;
}

TEST(MtkRdsPacketTest, RejectsWrongSize) {
    uint8_t buf[kPacketSize] = {};
    MtkRdsPacket out;
    EXPECT_FALSE(mtk_rds_parse(buf, 100, &out));
    EXPECT_FALSE(mtk_rds_parse(buf, kPacketSize + 1, &out));
    EXPECT_FALSE(mtk_rds_parse(nullptr, kPacketSize, &out));
}

TEST(MtkRdsPacketTest, AllZeroPacketProducesNothing) {
    uint8_t buf[kPacketSize];
    make_packet(buf, 0);

    MtkRdsPacket out;
    ASSERT_TRUE(mtk_rds_parse(buf, kPacketSize, &out));
    EXPECT_FALSE(out.has_pi);
    EXPECT_FALSE(out.has_ps);
    EXPECT_FALSE(out.has_rt);
    EXPECT_FALSE(out.has_pty);
    EXPECT_FALSE(out.has_ecc);
    EXPECT_FALSE(out.has_stereo);
    EXPECT_FALSE(out.has_af);
    EXPECT_FALSE(out.af_switch_hint);
}

TEST(MtkRdsPacketTest, ParsesPiPtyEcc) {
    uint8_t buf[kPacketSize];
    make_packet(buf, 0x0002 | 0x0004 | 0x1000);  // PI | PTY | ECC
    buf[24] = 0x34;                              // PI = 0x1234
    buf[25] = 0x12;
    buf[27] = 7;                                 // PTY
    buf[244] = 0xE2;                             // ECC

    MtkRdsPacket out;
    ASSERT_TRUE(mtk_rds_parse(buf, kPacketSize, &out));
    EXPECT_TRUE(out.has_pi);
    EXPECT_EQ(out.pi, 0x1234);
    EXPECT_TRUE(out.has_pty);
    EXPECT_EQ(out.pty, 7);
    EXPECT_TRUE(out.has_ecc);
    EXPECT_EQ(out.ecc, 0xE2);
}

TEST(MtkRdsPacketTest, ParsesProgramServiceFromWindowZero) {
    uint8_t buf[kPacketSize];
    make_packet(buf, 0x0008);  // PROGRAMNAME
    memcpy(buf + 248, "RADIO-98", 8);  // PS[0]

    MtkRdsPacket out;
    ASSERT_TRUE(mtk_rds_parse(buf, kPacketSize, &out));
    EXPECT_TRUE(out.has_ps);
    EXPECT_STREQ(out.ps, "RADIO-98");
}

TEST(MtkRdsPacketTest, SanitizesProgramService) {
    uint8_t buf[kPacketSize];
    make_packet(buf, 0x0008);
    const uint8_t raw[8] = {'A', 0x0A, 'B', 0x00, 'C', 0x7F, 'D', 0x01};
    memcpy(buf + 248, raw, 8);

    MtkRdsPacket out;
    ASSERT_TRUE(mtk_rds_parse(buf, kPacketSize, &out));
    // Control bytes become spaces; trailing spaces trimmed.
    EXPECT_STREQ(out.ps, "A B C D");
}

TEST(MtkRdsPacketTest, ParsesRadioTextWithLength) {
    uint8_t buf[kPacketSize];
    make_packet(buf, 0x0040);  // LAST_RADIOTEXT
    memcpy(buf + 290, "Hello FM radio", 14);
    buf[548] = 14;  // TextLength

    MtkRdsPacket out;
    ASSERT_TRUE(mtk_rds_parse(buf, kPacketSize, &out));
    EXPECT_TRUE(out.has_rt);
    EXPECT_STREQ(out.rt, "Hello FM radio");
}

TEST(MtkRdsPacketTest, ClampsRadioTextLength) {
    uint8_t buf[kPacketSize];
    make_packet(buf, 0x0040);
    memset(buf + 290, 'X', 64);
    buf[548] = 200;  // TextLength beyond the 64-byte window

    MtkRdsPacket out;
    ASSERT_TRUE(mtk_rds_parse(buf, kPacketSize, &out));
    EXPECT_TRUE(out.has_rt);
    EXPECT_EQ(strlen(out.rt), 64u);
}

TEST(MtkRdsPacketTest, ParsesAfListInHundredKhzUnits) {
    uint8_t buf[kPacketSize];
    make_packet(buf, 0x0100);  // AF_LIST
    const int16_t num = 3;
    memcpy(buf + 28, &num, 2);
    const int16_t af[3] = {996, 1003, 875};  // 99.6 / 100.3 / 87.5 MHz
    memcpy(buf + 30, af, sizeof(af));

    MtkRdsPacket out;
    ASSERT_TRUE(mtk_rds_parse(buf, kPacketSize, &out));
    EXPECT_TRUE(out.has_af);
    ASSERT_EQ(out.af_count, 3);
    EXPECT_EQ(out.af_khz[0], 99600);
    EXPECT_EQ(out.af_khz[1], 100300);
    EXPECT_EQ(out.af_khz[2], 87500);
}

TEST(MtkRdsPacketTest, AfListRejectsGarbage) {
    uint8_t buf[kPacketSize];
    make_packet(buf, 0x0100);
    const int16_t num = 30;  // beyond the 25-entry window
    memcpy(buf + 28, &num, 2);
    memset(buf + 30, 0xFF, 100);  // negative values

    MtkRdsPacket out;
    ASSERT_TRUE(mtk_rds_parse(buf, kPacketSize, &out));
    EXPECT_TRUE(out.has_af);
    EXPECT_EQ(out.af_count, 0);  // all entries invalid, nothing emitted
}

TEST(MtkRdsPacketTest, ParsesStereoFlag) {
    uint8_t buf[kPacketSize];
    make_packet(buf, 0x0001);  // FLAGS
    buf[15] = 1;  // RDSFlag.Stereo

    MtkRdsPacket out;
    ASSERT_TRUE(mtk_rds_parse(buf, kPacketSize, &out));
    EXPECT_TRUE(out.has_stereo);
    EXPECT_TRUE(out.stereo);

    buf[15] = 0;
    ASSERT_TRUE(mtk_rds_parse(buf, kPacketSize, &out));
    EXPECT_TRUE(out.has_stereo);
    EXPECT_FALSE(out.stereo);
}

TEST(MtkRdsPacketTest, ParsesAfSwitchHint) {
    uint8_t buf[kPacketSize];
    make_packet(buf, 0x0080);  // AF hint (weak signal)

    MtkRdsPacket out;
    ASSERT_TRUE(mtk_rds_parse(buf, kPacketSize, &out));
    EXPECT_TRUE(out.af_switch_hint);
}

}  // namespace

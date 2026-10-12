#include "rds_packet.h"

#include <string.h>

namespace {

// Copies up to `max` bytes, maps control bytes to spaces, right-trims.
void sanitize_text(const uint8_t *src, size_t len, char *out, size_t out_size) {
    size_t n = len < out_size - 1 ? len : out_size - 1;
    for (size_t i = 0; i < n; i++) {
        const uint8_t c = src[i];
        out[i] = (c < 0x20 || c == 0x7F) ? ' ' : static_cast<char>(c);
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
}

uint16_t read_le16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

int16_t read_le16s(const uint8_t *p) {
    return static_cast<int16_t>(read_le16(p));
}

}  // namespace

bool mtk_rds_parse(const uint8_t *buf, size_t len, MtkRdsPacket *out) {
    if (out == nullptr) {
        return false;
    }
    *out = MtkRdsPacket{};
    if (buf == nullptr || len != kMtkRdsPacketSize) {
        return false;
    }

    const uint16_t events = read_le16(buf + kMtkRdsOffEventStatus);

    if (events & kMtkRdsEventFlags) {
        out->has_stereo = true;
        out->stereo = buf[kMtkRdsOffStereoFlag] != 0;
    }

    if (events & kMtkRdsEventPi) {
        out->has_pi = true;
        out->pi = read_le16(buf + kMtkRdsOffPi);
    }

    if (events & kMtkRdsEventPty) {
        out->has_pty = true;
        out->pty = buf[kMtkRdsOffPty];
    }

    if (events & kMtkRdsEventPs) {
        out->has_ps = true;
        sanitize_text(buf + kMtkRdsOffPs, 8, out->ps, sizeof(out->ps));
    }

    if (events & kMtkRdsEventRt) {
        out->has_rt = true;
        size_t rt_len = buf[kMtkRdsOffRtLength];
        if (rt_len > 64) {
            rt_len = 64;
        }
        sanitize_text(buf + kMtkRdsOffRtText, rt_len, out->rt, sizeof(out->rt));
    }

    if (events & kMtkRdsEventEcc) {
        out->has_ecc = true;
        out->ecc = buf[kMtkRdsOffEcc];
    }

    if (events & kMtkRdsEventAfList) {
        out->has_af = true;
        int16_t num = read_le16s(buf + kMtkRdsOffAfNum);
        if (num > kMtkRdsMaxAf) {
            num = kMtkRdsMaxAf;
        }
        for (int16_t i = 0; i < num; i++) {
            const int16_t v = read_le16s(buf + kMtkRdsOffAfList + i * 2);
            if (v > 0) {
                out->af_khz[out->af_count++] = static_cast<int>(v) * 100;
            }
        }
    }

    out->af_switch_hint = (events & kMtkRdsEventAfHint) != 0;

    return true;
}

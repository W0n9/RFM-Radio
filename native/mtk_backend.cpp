// MediaTek backend: drives the tuner directly through /dev/fm ioctls
// (fmradio_drv_mt6631_6635). No vendor libraries are involved.
//
// Scope of this file (ticket 01): init/enable/disable/set_frequency/RMSSI.
// seek/search/jump are ticket 04; runtime region/spacing semantics are
// ticket 05; RDS arrives in ticket 03.

#include "backend.h"
#include "ctl_server.h"
#include "monotonic_clock.h"
#include "region_profile.h"

#include "mtk/mtk_dev.h"
#include "mtk/mtk_log.h"
#include "mtk/mtk_params.h"

#include <stdio.h>

namespace {

constexpr const char *kErrFailed = "ERR_FAILED";
constexpr const char *kErrUnsupported = "ERR_UNSUPPORTED";
constexpr const char *kErrInvalidAntenna = "ERR_UNV_ANT";
constexpr uint64_t kMetricSettleMs = 300;

class MtkBackend final : public Backend {
public:
    BackendKind kind() const override {
        return BackendKind::kMtk;
    }

    bool init() override {
        // Probe only: open, identify, close. Keeps permission/driver problems
        // on the launch path instead of enable, and avoids holding a stale fd.
        const int fd = mtk_dev_open();
        if (fd < 0) {
            return fail_errno("open");
        }

        uint16_t chip_id = 0;
        int32_t rds_support = 0;
        uint32_t powered = 0;
        mtk_get_chip_id(fd, &chip_id);
        mtk_rds_support(fd, &rds_support);
        mtk_is_powered_up(fd, &powered);
        mtk_log("init", "chip_id=0x%x rds_support=%d powered=%u", chip_id, rds_support, powered);

        mtk_dev_close(fd);
        return set_ok();
    }

    bool enable(const StartupConfig &config) override {
        profile_ = &get_region_profile(config.region);
        spacing_khz_ = config.spacing_khz;

        mtk_log("enable", "requested region=%s mtk_band=%d spacing_khz=%d stereo=%d soft_mute=%d antenna=%d af=%d frequency_khz=%u",
                get_region_name(config.region),
                profile_->mtk_band,
                spacing_khz_,
                config.stereo ? 1 : 0,
                config.soft_mute ? 1 : 0,
                config.antenna,
                config.auto_af ? 1 : 0,
                config.frequency_khz);

        fd_ = mtk_dev_open();
        if (fd_ < 0) {
            return fail_errno("open");
        }

        // The driver understands exactly two antenna values.
        if (config.antenna != 0 && config.antenna != 1) {
            mtk_dev_close(fd_);
            fd_ = -1;
            return fail(kErrInvalidAntenna);
        }
        // Best-effort: short-antenna support is device-gated, so a failure
        // here must not kill the whole enable.
        mtk_set_antenna(fd_, config.antenna);

        if (!tune_to(config.frequency_khz, /*power_up=*/true)) {
            return rollback_errno("powerup");
        }

        if (!mtk_set_stereo_auto(fd_, config.stereo)) {
            return rollback_errno("stereo");
        }

        // Unmute last, mirroring the legacy ordering rule.
        if (!mtk_set_mute(fd_, false)) {
            return rollback_errno("unmute");
        }

        enabled_ = true;
        mtk_log("enable", "accepted frequency_khz=%u", current_frequency_khz_);
        return set_ok();
    }

    bool disable() override {
        bool ok = true;
        if (fd_ >= 0) {
            ok &= mtk_set_mute(fd_, true);
            ok &= mtk_power_down(fd_);
            // Driver-side confirmation that the chip is really off.
            uint32_t powered = 1;
            if (mtk_is_powered_up(fd_, &powered)) {
                mtk_log("disable", "powered=%u after powerdown", powered);
            }
            mtk_dev_close(fd_);
            fd_ = -1;
        }
        enabled_ = false;
        mtk_log("disable", ok ? "done" : "done with errors");
        send_native_event("disabled");
        return ok ? set_ok() : fail(kErrFailed);
    }

    bool set_frequency(uint32_t frequency_khz) override {
        if (!enabled_) {
            return fail(kErrFailed);
        }
        return tune_to(frequency_khz, /*power_up=*/false);
    }

    bool jump(int /*direction*/, uint32_t * /*new_frequency*/) override {
        return fail(kErrUnsupported);  // ticket 04
    }

    bool seek(int /*direction*/) override {
        return fail(kErrUnsupported);  // ticket 04
    }

    bool set_power_mode(bool low_power) override {
        // No MTK counterpart; no-op, mirroring the legacy slimbus precedent.
        mtk_log("setup", "set power_low=%d (noop)", low_power ? 1 : 0);
        return set_ok();
    }

    bool set_stereo(bool enabled) override {
        mtk_log("audio", "set stereo=%d", enabled ? 1 : 0);
        if (!enabled_) {
            return set_ok();
        }
        return mtk_set_stereo_auto(fd_, enabled) ? set_ok() : fail_errno("stereo");
    }

    bool set_antenna(int antenna) override {
        mtk_log("setup", "set antenna=%d", antenna);
        if (antenna != 0 && antenna != 1) {
            return fail(kErrInvalidAntenna);
        }
        if (!enabled_) {
            return set_ok();
        }
        return mtk_set_antenna(fd_, antenna) ? set_ok() : fail_errno("antenna");
    }

    bool set_region(StartupRegion region) override {
        // Cache-only for now; live band re-application (with re-POWERUP
        // fallback) is ticket 05.
        profile_ = &get_region_profile(region);
        mtk_log("setup", "set region=%s (applies on next enable)", get_region_name(region));
        return set_ok();
    }

    bool set_spacing(int spacing_khz) override {
        // Cache-only; live re-tune is ticket 05.
        spacing_khz_ = spacing_khz;
        mtk_log("setup", "set spacing_khz=%d (applies on next enable)", spacing_khz);
        return set_ok();
    }

    bool search() override {
        return fail(kErrUnsupported);  // ticket 04
    }

    bool cancel_search() override {
        return fail(kErrUnsupported);  // ticket 04
    }

    bool set_auto_af(bool enabled) override {
        mtk_log("af", "set auto=%d (noop)", enabled ? 1 : 0);
        return set_ok();
    }

    bool set_soft_mute(bool enabled) override {
        mtk_log("audio", "set soft_mute=%d (noop)", enabled ? 1 : 0);
        return set_ok();
    }

    bool set_slimbus(bool /*enabled*/) override {
        return set_ok();
    }

    bool request_rmssi() override {
        if (!enabled_ || monotonic_ms() < metric_resume_at_ms_) {
            return false;
        }

        int32_t rssi = 0;
        if (!mtk_get_rssi(fd_, &rssi)) {
            return false;
        }

        radio_state_patch_t patch = radio_state_patch_empty();
        patch.rmssi = rssi;
        return send_radio_state_patch(&patch);
    }

    const char *last_error() const override {
        return last_error_;
    }

private:
    // Shared tune path for enable (power-up) and runtime retune. Clamps to the
    // current region, adopts the driver-snapped frequency, arms the RMSSI
    // settle window, and publishes the frequency patch.
    bool tune_to(uint32_t frequency_khz, bool power_up) {
        const uint32_t clamped = clamp_frequency_to_region(*profile_, frequency_khz);
        fm_tune_parm parm = mtk_make_tune_parm(
                static_cast<uint8_t>(profile_->mtk_band),
                spacing_khz_,
                profile_->emphasis,
                clamped);
        const bool ok = power_up ? mtk_power_up(fd_, &parm) : mtk_tune(fd_, &parm);
        if (!ok) {
            return false;
        }
        current_frequency_khz_ = khz_from_mtk_freq(parm.freq);
        metric_resume_at_ms_ = monotonic_ms() + kMetricSettleMs;
        publish_frequency();
        return true;
    }

    void publish_frequency() {
        radio_state_patch_t patch = radio_state_patch_empty();
        patch.frequency_khz = static_cast<int>(current_frequency_khz_);
        send_radio_state_patch(&patch);
    }

    bool rollback_errno(const char *op) {
        fail_errno(op);
        if (fd_ >= 0) {
            mtk_power_down(fd_);
            mtk_dev_close(fd_);
            fd_ = -1;
        }
        enabled_ = false;
        return false;
    }

    bool set_ok() {
        last_error_[0] = '\0';
        return true;
    }

    bool fail(const char *error) {
        snprintf(last_error_, sizeof(last_error_), "%s", error);
        return false;
    }

    bool fail_errno(const char *op) {
        snprintf(last_error_, sizeof(last_error_), "%s errno=%d", op, errno);
        return false;
    }

    int fd_ = -1;
    bool enabled_ = false;
    const RegionProfile *profile_ = &get_region_profile(StartupRegion::kEurope);
    int spacing_khz_ = 100;
    uint32_t current_frequency_khz_ = 87500;
    uint64_t metric_resume_at_ms_ = 0;
    char last_error_[96] = "";
};

}  // namespace

Backend *create_mtk_backend() {
    static MtkBackend backend;
    return &backend;
}

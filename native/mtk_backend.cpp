// MediaTek backend: drives the tuner directly through /dev/fm ioctls
// (fmradio_drv_mt6631_6635). No vendor libraries are involved.
//
// Scope: tickets 01+03+04+05 — init/enable/disable/set_frequency/jump/seek/
// scan/RMSSI + RDS (reader thread + rds_t parsing) + runtime region/spacing.
//
// RDS read semantics (verified against kernel source fm_rds_read):
// read() never blocks — it returns one 688-byte rds_t when a parsed RDS
// event is pending, or 0 when there is none. The reader therefore polls
// with a sleep, and can never hang disable().
//
// Threading/lock ownership:
// - The command thread (ctl_server) owns fd_: only it opens/closes.
// - The RDS reader thread only read()s the fd; the scan worker only ioctl()s
//   it, and both are provably stopped before close (RDS reads never block;
//   the scan worker checks the cancel flag between bounded steps and
//   disable() flag-waits for its exit — no detach, ever).
// - state_lock_ guards the mutable station context shared by all three:
//   current_frequency_khz_, metric_resume_at_ms_, last_pi_, last_ecc_,
//   pi_text_.

#include "backend.h"
#include "ctl_server.h"
#include "monotonic_clock.h"
#include "rds_parser.h"
#include "region_profile.h"
#include "scan_result.h"

#include "mtk/mtk_dev.h"
#include "mtk/mtk_log.h"
#include "mtk/mtk_params.h"
#include "mtk/rds_packet.h"
#include "mtk/step_scan.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <atomic>

namespace {

constexpr const char *kErrFailed = "ERR_FAILED";
constexpr const char *kErrInvalidAntenna = "ERR_UNV_ANT";
constexpr uint64_t kMetricSettleMs = 300;
constexpr int kRdsIdlePollMs = 500;
// Step-scan parameters. Threshold calibrated on cannon against the stock
// app's results: audible stations land at -74..-88 dBm, noise/interference
// haze starts at -90 dBm.
constexpr uint32_t kScanSettleMs = 60;
constexpr int32_t kScanThresholdDbm = -88;
constexpr uint32_t kScanDeadlinePerStepMs = 200;
constexpr uint64_t kSeekDeadlineMs = 60000;

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

        // RDS on + reader thread before unmute, mirroring the legacy
        // "RDS setup before unmute" ordering rule.
        if (!start_rds()) {
            return rollback_errno("rds");
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
        // Stop the scan worker before touching the fd. The step loop checks
        // the cancel flag between bounded steps, so it always exits; wait for
        // it (never detach — a detached worker would touch a closed fd).
        if (scan_busy_.load()) {
            scan_cancel_.store(true);
            int waited_ms = 0;
            while (!scan_done_.load()) {
                usleep(50 * 1000);
                waited_ms += 50;
                if (waited_ms % 2000 == 0) {
                    mtk_log("scan", "wait worker stop waited_ms=%d", waited_ms);
                }
            }
            join_scan_thread();
        }

        bool ok = true;
        stop_rds();
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
        if (!enabled_ || scan_busy_.load()) {
            return fail(kErrFailed);
        }
        return tune_to(frequency_khz, /*power_up=*/false);
    }

    bool jump(int direction, uint32_t *new_frequency) override {
        if (!enabled_ || scan_busy_.load()) {
            return fail(kErrFailed);
        }
        // One spacing step, wrapping inside the region (HAL-style).
        const uint32_t current = current_frequency();
        const int64_t stepped = static_cast<int64_t>(current)
                + (direction >= 0 ? spacing_khz_ : -spacing_khz_);
        uint32_t target = static_cast<uint32_t>(stepped);
        if (stepped > profile_->upper_frequency_khz) {
            target = profile_->lower_frequency_khz;
        } else if (stepped < profile_->lower_frequency_khz) {
            target = profile_->upper_frequency_khz;
        }
        if (!tune_to(target, /*power_up=*/false)) {
            return fail_errno("tune");
        }
        if (new_frequency != nullptr) {
            *new_frequency = current_frequency();
        }
        return set_ok();
    }

    bool seek(int direction) override {
        return start_step_scan(direction >= 0 ? StepScanMode::kSeekUp : StepScanMode::kSeekDown);
    }

    bool set_power_mode(bool low_power) override {
        // No MTK counterpart; no-op, mirroring the legacy slimbus precedent.
        mtk_log("setup", "set power_low=%d (noop)", low_power ? 1 : 0);
        return set_ok();
    }

    bool set_stereo(bool enabled) override {
        if (scan_busy_.load()) {
            return fail(kErrFailed);
        }
        mtk_log("audio", "set stereo=%d", enabled ? 1 : 0);
        if (!enabled_) {
            return set_ok();
        }
        return mtk_set_stereo_auto(fd_, enabled) ? set_ok() : fail_errno("stereo");
    }

    bool set_antenna(int antenna) override {
        if (scan_busy_.load()) {
            return fail(kErrFailed);
        }
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
        if (scan_busy_.load()) {
            return fail(kErrFailed);
        }
        profile_ = &get_region_profile(region);
        mtk_log("setup", "set region=%s mtk_band=%d", get_region_name(region), profile_->mtk_band);
        if (!enabled_) {
            return set_ok();
        }
        // Try a plain re-tune carrying the new band; if the driver rejects a
        // band change without re-power-up, fall back to POWERDOWN+POWERUP and
        // replay the post-power-up tail (stereo, RDS, unmute) — the same
        // steps enable() runs, since a power cycle resets them all.
        const uint32_t target = clamp_frequency_to_region(*profile_, current_frequency());
        if (tune_to(target, /*power_up=*/false)) {
            return set_ok();
        }
        mtk_log("setup", "region retune failed, re-powering");
        fm_tune_parm parm = mtk_make_tune_parm(
                static_cast<uint8_t>(profile_->mtk_band),
                spacing_khz_,
                profile_->emphasis,
                target);
        if (!mtk_power_down(fd_) || !mtk_power_up(fd_, &parm)) {
            return fail_errno("re-power");
        }
        // Re-apply the runtime state the power cycle just wiped.
        mtk_set_stereo_auto(fd_, true);
        mtk_rds_onoff(fd_, false);
        mtk_rds_onoff(fd_, true);
        mtk_set_mute(fd_, false);

        pthread_mutex_lock(&state_lock_);
        current_frequency_khz_ = khz_from_mtk_freq(parm.freq);
        metric_resume_at_ms_ = monotonic_ms() + kMetricSettleMs;
        pthread_mutex_unlock(&state_lock_);
        publish_tune();
        return set_ok();
    }

    bool set_spacing(int spacing_khz) override {
        if (scan_busy_.load()) {
            return fail(kErrFailed);
        }
        spacing_khz_ = spacing_khz;
        mtk_log("setup", "set spacing_khz=%d", spacing_khz);
        if (!enabled_) {
            return set_ok();
        }
        // Re-tune so the new grid applies immediately.
        return tune_to(current_frequency(), /*power_up=*/false);
    }

    bool search() override {
        return start_step_scan(StepScanMode::kFullScan);
    }

    bool cancel_search() override {
        if (!scan_busy_.load()) {
            return fail(kErrFailed);
        }
        scan_cancel_.store(true);
        return set_ok();
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
        if (!enabled_ || scan_busy_.load()) {
            return false;
        }
        pthread_mutex_lock(&state_lock_);
        const uint64_t resume_at = metric_resume_at_ms_;
        pthread_mutex_unlock(&state_lock_);
        if (monotonic_ms() < resume_at) {
            return false;
        }

        int32_t rssi = 0;
        if (!mtk_get_rssi(fd_, &rssi)) {
            // A connsys restart (e.g. airplane mode) kills the chip under us;
            // degrade to disabled instead of spinning on a dead fd. Transient
            // failures just skip this round.
            if (++consecutive_ioctl_failures_ >= 3) {
                mtk_log("event", "ioctl failures=%d, degrading to disabled", consecutive_ioctl_failures_);
                consecutive_ioctl_failures_ = 0;
                disable();
            }
            return false;
        }
        consecutive_ioctl_failures_ = 0;

        radio_state_patch_t patch = radio_state_patch_empty();
        patch.rmssi = rssi;
        return send_radio_state_patch(&patch);
    }

    const char *last_error() const override {
        return last_error_;
    }

private:
    // --- RDS reader thread (read-only fd user; never blocks) ---

    bool start_rds() {
        if (!mtk_rds_onoff(fd_, true)) {
            return false;
        }

        rds_stop_.store(false);
        pthread_mutex_lock(&state_lock_);
        last_pi_ = 0;
        last_ecc_ = -1;
        pthread_mutex_unlock(&state_lock_);
        const int rc = pthread_create(&rds_thread_, nullptr, &rds_reader_entry, this);
        if (rc != 0) {
            mtk_log("rds", "pthread_create failed rc=%d", rc);
            mtk_rds_onoff(fd_, false);
            return false;
        }
        rds_thread_running_ = true;
        return true;
    }

    void stop_rds() {
        if (!rds_thread_running_) {
            return;
        }
        rds_stop_.store(true);
        // Reads never block, so the loop always exits within one poll slice.
        const int rc = pthread_join(rds_thread_, nullptr);
        rds_thread_running_ = false;
        if (rc != 0) {
            mtk_log("rds", "join failed rc=%d", rc);
        }
        if (fd_ >= 0) {
            mtk_rds_onoff(fd_, false);
        }
    }

    static void *rds_reader_entry(void *arg) {
        static_cast<MtkBackend *>(arg)->rds_read_loop();
        return nullptr;
    }

    void rds_read_loop() {
        uint8_t buf[kMtkRdsPacketSize];
        while (!rds_stop_.load()) {
            const ssize_t n = read(fd_, buf, sizeof(buf));
            if (n == static_cast<ssize_t>(sizeof(buf))) {
                handle_rds_packet(buf);
                continue;
            }
            if (n < 0) {
                // Non-blocking semantics make a bare 0 the "no data" answer;
                // a negative return is a real error worth logging.
                mtk_log("rds", "read fail ret=%zd errno=%d", n, errno);
            } else if (n > 0) {
                // Should not happen (the driver delivers whole packets);
                // log and drop rather than parse a torn packet.
                mtk_log("rds", "short read ret=%zd", n);
            }
            // No data (or transient error): sleep in slices so disable() is
            // responsive.
            for (int i = 0; i < kRdsIdlePollMs / 100 && !rds_stop_.load(); i++) {
                usleep(100 * 1000);
            }
        }
    }

    void handle_rds_packet(const uint8_t *buf) {
        MtkRdsPacket packet;
        if (!mtk_rds_parse(buf, kMtkRdsPacketSize, &packet)) {
            return;
        }

        pthread_mutex_lock(&state_lock_);

        radio_state_patch_t patch = radio_state_patch_empty();
        bool has_content = false;

        if (packet.has_pi) {
            last_pi_ = packet.pi;
            snprintf(pi_text_, sizeof(pi_text_), "%04x", packet.pi);
            patch.pi = pi_text_;
            has_content = true;
        }
        if (packet.has_pty) {
            patch.pty = packet.pty;
            has_content = true;
        }
        if (packet.has_ps) {
            patch.ps = packet.ps;
            has_content = true;
        }
        if (packet.has_rt) {
            patch.rt = packet.rt;
            has_content = true;
        }
        if (packet.has_ecc) {
            last_ecc_ = packet.ecc;
        }
        // Country needs ECC + PI; they may arrive in different packets, so
        // re-decode whenever either side refreshed and both are known.
        if (last_ecc_ >= 0 && last_pi_ != 0) {
            char country[3];
            if (decode_rds_country_iso(last_ecc_, last_pi_ >> 12, country)) {
                patch.country = country;
                has_content = true;
            }
        }
        if (packet.has_af) {
            patch.af_khz = packet.af_khz;
            patch.af_count = packet.af_count;
            has_content = true;
        }
        if (packet.has_stereo) {
            patch.stereo = packet.stereo ? 1 : 0;
            has_content = true;
        }
        if (packet.af_switch_hint) {
            mtk_log("event", "af_hint=1");
        }

        if (has_content) {
            // The send serializes the patch synchronously, while pi_text_ is
            // still referenced — stay locked through it.
            send_radio_state_patch(&patch);
        }

        pthread_mutex_unlock(&state_lock_);
    }

    // --- step scan (ticket 04) ---

    void join_scan_thread() {
        if (scan_thread_joinable_) {
            pthread_join(scan_thread_, nullptr);
            scan_thread_joinable_ = false;
        }
    }

    bool start_step_scan(StepScanMode mode) {
        if (!enabled_ || scan_busy_.load()) {
            return fail(kErrFailed);
        }
        // Reap the previous finished worker (it exited but was never joined).
        join_scan_thread();
        scan_cancel_.store(false);
        scan_done_.store(false);
        scan_busy_.store(true);
        scan_mode_ = mode;
        const int rc = pthread_create(&scan_thread_, nullptr, &scan_entry, this);
        if (rc != 0) {
            scan_busy_.store(false);
            scan_done_.store(true);
            mtk_log("scan", "pthread_create failed rc=%d", rc);
            return fail(kErrFailed);
        }
        scan_thread_joinable_ = true;
        return set_ok();
    }

    static void *scan_entry(void *arg) {
        static_cast<MtkBackend *>(arg)->scan_worker_main();
        return nullptr;
    }

    bool scan_tune(uint32_t frequency_khz) {
        scan_step_khz_ = frequency_khz;
        fm_tune_parm parm = mtk_make_tune_parm(
                static_cast<uint8_t>(profile_->mtk_band),
                spacing_khz_,
                profile_->emphasis,
                frequency_khz);
        return mtk_tune(fd_, &parm);
    }

    void scan_worker_main() {
        const uint32_t steps = (profile_->upper_frequency_khz - profile_->lower_frequency_khz)
                / static_cast<uint32_t>(spacing_khz_) + 1;
        const bool is_scan = scan_mode_ == StepScanMode::kFullScan;

        StepScanConfig config = {
                .lower_khz = profile_->lower_frequency_khz,
                .upper_khz = profile_->upper_frequency_khz,
                .start_khz = current_frequency(),
                .step_khz = spacing_khz_,
                .threshold_dbm = kScanThresholdDbm,
                .settle_ms = kScanSettleMs,
                .deadline_ms = is_scan
                        ? monotonic_ms() + steps * (kScanSettleMs + kScanDeadlinePerStepMs)
                        : monotonic_ms() + kSeekDeadlineMs,
        };
        StepScanEnv env = {&env_tune, &env_rssi, &env_cancel, &env_now, &env_sleep, this};

        mtk_log("scan", "start mode=%d lower_khz=%u upper_khz=%u spacing_khz=%d threshold_dbm=%d",
                static_cast<int>(scan_mode_), config.lower_khz, config.upper_khz,
                config.step_khz, config.threshold_dbm);

        const StepScanOutcome outcome = step_scan_run(scan_mode_, config, env, is_scan ? &scan_collector_ : nullptr);

        switch (outcome.status) {
            case StepScanStatus::kCompleted: {
                ScanResult result;
                scan_collector_.copy_sorted(&result);
                mtk_log("scan", "completed count=%d", result.count);
                send_search_done(result.frequencies_khz, result.count);
                scan_tune(outcome.resume_frequency_khz);
                break;
            }
            case StepScanStatus::kFound: {
                pthread_mutex_lock(&state_lock_);
                current_frequency_khz_ = outcome.found_frequency_khz;
                metric_resume_at_ms_ = monotonic_ms() + kMetricSettleMs;
                pthread_mutex_unlock(&state_lock_);
                publish_tune();
                mtk_log("event", "seek found frequency_khz=%u", outcome.found_frequency_khz);
                break;
            }
            case StepScanStatus::kNotFound:
            case StepScanStatus::kCancelled:
            case StepScanStatus::kAborted: {
                mtk_log("scan", "end status=%d", static_cast<int>(outcome.status));
                scan_tune(outcome.resume_frequency_khz);
                break;
            }
        }

        scan_busy_.store(false);
        scan_done_.store(true);
    }

    static bool env_tune(uint32_t frequency_khz, void *ctx) {
        return static_cast<MtkBackend *>(ctx)->scan_tune(frequency_khz);
    }

    static bool env_rssi(int32_t *out, void *ctx) {
        MtkBackend *self = static_cast<MtkBackend *>(ctx);
        const bool ok = mtk_get_rssi(self->fd_, out);
        if (ok) {
            mtk_log("scan", "step frequency_khz=%u rssi_dbm=%d", self->scan_step_khz_, *out);
        }
        return ok;
    }

    static bool env_cancel(void *ctx) {
        return static_cast<MtkBackend *>(ctx)->scan_cancel_.load();
    }

    static uint64_t env_now(void * /*ctx*/) {
        return monotonic_ms();
    }

    static void env_sleep(uint32_t ms, void * /*ctx*/) {
        usleep(ms * 1000);
    }

    // --- shared tune path ---

    // Clamps to the current region, tunes (or powers up), adopts the
    // driver-snapped frequency, arms the RMSSI settle window, and publishes
    // the tune patch. The ioctl runs outside the lock; the shared station
    // state is updated under state_lock_.
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
        pthread_mutex_lock(&state_lock_);
        current_frequency_khz_ = khz_from_mtk_freq(parm.freq);
        metric_resume_at_ms_ = monotonic_ms() + kMetricSettleMs;
        pthread_mutex_unlock(&state_lock_);
        publish_tune();
        return true;
    }

    // Frequency + cleared station metadata, mirroring the legacy TUNE_SUCC
    // patch.
    void publish_tune() {
        pthread_mutex_lock(&state_lock_);
        last_pi_ = 0;
        last_ecc_ = -1;

        radio_state_patch_t patch = radio_state_patch_empty();
        patch.frequency_khz = static_cast<int>(current_frequency_khz_);
        patch.ps = patch.rt = patch.pi = patch.country = "";
        patch.af_count = patch.pty = 0;
        send_radio_state_patch(&patch);
        pthread_mutex_unlock(&state_lock_);
    }

    uint32_t current_frequency() {
        pthread_mutex_lock(&state_lock_);
        const uint32_t frequency = current_frequency_khz_;
        pthread_mutex_unlock(&state_lock_);
        return frequency;
    }

    bool rollback_errno(const char *op) {
        fail_errno(op);
        stop_rds();
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
    char last_error_[96] = "";

    pthread_mutex_t state_lock_ = PTHREAD_MUTEX_INITIALIZER;
    uint32_t current_frequency_khz_ = 87500;
    uint64_t metric_resume_at_ms_ = 0;
    uint16_t last_pi_ = 0;
    int last_ecc_ = -1;
    char pi_text_[5] = "";

    pthread_t rds_thread_{};
    std::atomic<bool> rds_stop_{false};
    bool rds_thread_running_ = false;

    pthread_t scan_thread_{};
    std::atomic<bool> scan_busy_{false};
    std::atomic<bool> scan_cancel_{false};
    std::atomic<bool> scan_done_{true};
    bool scan_thread_joinable_ = false;
    StepScanMode scan_mode_ = StepScanMode::kFullScan;
    ScanResultCollector scan_collector_;
    uint32_t scan_step_khz_ = 0;
    int consecutive_ioctl_failures_ = 0;
};

}  // namespace

Backend *create_mtk_backend() {
    static MtkBackend backend;
    return &backend;
}

#include "step_scan.h"

namespace {

bool scan_cancelled(const StepScanEnv &env) {
    return env.should_cancel != nullptr && env.should_cancel(env.ctx);
}

bool scan_expired(const StepScanConfig &config, const StepScanEnv &env) {
    return env.now_ms != nullptr && env.now_ms(env.ctx) >= config.deadline_ms;
}

// Tunes, settles, samples. Returns false when the driver rejected the step
// (the channel is skipped, the walk continues).
bool probe(uint32_t frequency_khz, const StepScanConfig &config, const StepScanEnv &env, int32_t *rssi_dbm) {
    if (!env.tune(frequency_khz, env.ctx)) {
        return false;
    }
    if (env.sleep_ms != nullptr && config.settle_ms > 0) {
        env.sleep_ms(config.settle_ms, env.ctx);
    }
    return env.rssi_dbm(rssi_dbm, env.ctx);
}

StepScanOutcome run_scan(const StepScanConfig &config, const StepScanEnv &env, ScanResultCollector *collector) {
    StepScanOutcome outcome;
    outcome.resume_frequency_khz = config.start_khz;

    // One real station bleeds onto adjacent channels; keep only local maxima
    // (a channel stronger than both neighbors) so one station lists once.
    // Evaluated with one step of lag: the previous channel is judged once the
    // current one is known. Failed probes count as -inf but keep the chain.
    constexpr int32_t kNoSignal = INT32_MIN;
    bool have_prev = false;
    uint32_t prev_freq = 0;
    int32_t prev_rssi = kNoSignal;
    int32_t prev2_rssi = kNoSignal;

    for (uint32_t freq = config.lower_khz; freq <= config.upper_khz; freq += config.step_khz) {
        if (scan_cancelled(env)) {
            outcome.status = StepScanStatus::kCancelled;
            return outcome;
        }
        if (scan_expired(config, env)) {
            outcome.status = StepScanStatus::kAborted;
            return outcome;
        }

        int32_t rssi_dbm = kNoSignal;
        if (!probe(freq, config, env, &rssi_dbm)) {
            rssi_dbm = kNoSignal;
        }

        if (have_prev
                && prev_rssi >= config.threshold_dbm
                && prev_rssi >= prev2_rssi
                && prev_rssi > rssi_dbm) {
            collector->add(static_cast<int>(prev_freq));
        }

        prev2_rssi = prev_rssi;
        prev_rssi = rssi_dbm;
        prev_freq = freq;
        have_prev = true;
    }

    // Judge the final channel against a virtual right neighbor at -inf.
    if (have_prev && prev_rssi >= config.threshold_dbm && prev_rssi >= prev2_rssi) {
        collector->add(static_cast<int>(prev_freq));
    }

    outcome.status = StepScanStatus::kCompleted;
    return outcome;
}

StepScanOutcome run_seek(int direction, const StepScanConfig &config, const StepScanEnv &env) {
    StepScanOutcome outcome;
    outcome.resume_frequency_khz = config.start_khz;

    const int step = config.step_khz * direction;
    uint32_t current = config.start_khz;
    bool wrapped = false;

    while (true) {
        if (scan_cancelled(env)) {
            outcome.status = StepScanStatus::kCancelled;
            return outcome;
        }
        if (scan_expired(config, env)) {
            outcome.status = StepScanStatus::kAborted;
            return outcome;
        }

        int64_t next = static_cast<int64_t>(current) + step;
        if (next < config.lower_khz || next > config.upper_khz) {
            if (wrapped) {
                outcome.status = StepScanStatus::kNotFound;
                return outcome;
            }
            wrapped = true;
            next = direction > 0 ? config.lower_khz : config.upper_khz;
        }
        if (static_cast<uint32_t>(next) == config.start_khz) {
            // Full circle without a hit.
            outcome.status = StepScanStatus::kNotFound;
            return outcome;
        }

        int32_t rssi_dbm = 0;
        if (probe(static_cast<uint32_t>(next), config, env, &rssi_dbm) && rssi_dbm >= config.threshold_dbm) {
            outcome.status = StepScanStatus::kFound;
            outcome.found_frequency_khz = static_cast<uint32_t>(next);
            outcome.resume_frequency_khz = outcome.found_frequency_khz;
            return outcome;
        }

        current = static_cast<uint32_t>(next);
    }
}

}  // namespace

StepScanOutcome step_scan_run(
        StepScanMode mode,
        const StepScanConfig &config,
        const StepScanEnv &env,
        ScanResultCollector *collector) {
    StepScanOutcome invalid;
    invalid.status = StepScanStatus::kAborted;
    invalid.resume_frequency_khz = config.start_khz;
    if (env.tune == nullptr || env.rssi_dbm == nullptr || config.step_khz <= 0
            || config.lower_khz > config.upper_khz) {
        return invalid;
    }

    switch (mode) {
        case StepScanMode::kFullScan:
            if (collector == nullptr) {
                return invalid;
            }
            collector->reset();
            return run_scan(config, env, collector);

        case StepScanMode::kSeekUp:
            return run_seek(1, config, env);

        case StepScanMode::kSeekDown:
            return run_seek(-1, config, env);
    }
    return invalid;
}

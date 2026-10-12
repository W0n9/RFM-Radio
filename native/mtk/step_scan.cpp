#include "step_scan.h"

namespace {

bool scan_cancelled(const StepScanEnv &env) {
    return env.should_cancel != nullptr && env.should_cancel(env.ctx);
}

bool scan_expired(const StepScanConfig &config, const StepScanEnv &env) {
    return env.now_ms != nullptr && env.now_ms(env.ctx) >= config.deadline_ms;
}

// A channel counts only when the kernel judged it valid AND it clears our
// own level floor (the kernel's own RSSI threshold is looser than ours).
// Everything else — rejected probe, invalid verdict, weak level — becomes
// kNoSignal, which keeps the local-peak chain intact.
bool probe(uint32_t frequency_khz, const StepScanConfig &config, const StepScanEnv &env, int32_t *rssi_raw) {
    int32_t level = 0;
    int32_t valid = 0;
    if (!env.probe(frequency_khz, &level, &valid, env.ctx) || !valid) {
        return false;
    }
    if (level < config.threshold_raw) {
        return false;
    }
    *rssi_raw = level;
    return true;
}

StepScanOutcome run_scan(const StepScanConfig &config, const StepScanEnv &env, ScanResultCollector *collector) {
    StepScanOutcome outcome;
    outcome.resume_frequency_khz = config.start_khz;

    // One real station bleeds onto adjacent channels; keep only local maxima
    // (a channel stronger than both neighbors) so one station lists once.
    // Evaluated with one step of lag: the previous channel is judged once the
    // current one is known. Rejected probes count as -inf but keep the chain.
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

        int32_t rssi_raw = kNoSignal;
        if (!probe(freq, config, env, &rssi_raw)) {
            rssi_raw = kNoSignal;
        }

        if (have_prev
                && prev_rssi >= config.threshold_raw
                && prev_rssi >= prev2_rssi
                && prev_rssi > rssi_raw) {
            collector->add(static_cast<int>(prev_freq));
        }

        prev2_rssi = prev_rssi;
        prev_rssi = rssi_raw;
        prev_freq = freq;
        have_prev = true;
    }

    // Judge the final channel against a virtual right neighbor at -inf.
    if (have_prev && prev_rssi >= config.threshold_raw && prev_rssi >= prev2_rssi) {
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

        int32_t hit_rssi = 0;
        if (probe(static_cast<uint32_t>(next), config, env, &hit_rssi)) {
            // The first hit is often a shoulder of a wider carrier. Walk the
            // next two channels in the seek direction and keep the strongest
            // (MIUI's seek does the same) so we land on the peak, not a
            // side-lobe.
            uint32_t best = static_cast<uint32_t>(next);
            int32_t best_rssi = hit_rssi;
            for (int i = 1; i <= 2; i++) {
                const int64_t shoulder = static_cast<int64_t>(next) + static_cast<int64_t>(step) * i;
                if (shoulder < config.lower_khz || shoulder > config.upper_khz) {
                    break;
                }
                if (scan_cancelled(env)) {
                    outcome.status = StepScanStatus::kCancelled;
                    return outcome;
                }
                if (scan_expired(config, env)) {
                    outcome.status = StepScanStatus::kAborted;
                    return outcome;
                }
                int32_t shoulder_rssi = 0;
                if (!probe(static_cast<uint32_t>(shoulder), config, env, &shoulder_rssi)
                        || shoulder_rssi <= best_rssi) {
                    break;
                }
                best = static_cast<uint32_t>(shoulder);
                best_rssi = shoulder_rssi;
            }

            outcome.status = StepScanStatus::kFound;
            outcome.found_frequency_khz = best;
            outcome.resume_frequency_khz = best;
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
    if (env.probe == nullptr || config.step_khz <= 0
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

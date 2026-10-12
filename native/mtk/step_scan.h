#pragma once

// Userspace step-scan state machine for the MTK backend (the driver has no
// hardware seek/scan ioctl — but each channel is probed with the kernel's
// SOFT_MUTE_TUNE, which tunes, samples, and judges inside one blocking
// ioctl, so there is no userspace settle time). Pure logic: the environment
// (probe/cancel/clock) is injected, so this is host-testable. The scan
// collects into the shared ScanResultCollector.

#include <stdint.h>

#include "scan_result.h"

enum class StepScanMode {
    kSeekUp,
    kSeekDown,
    kFullScan,
};

enum class StepScanStatus {
    kFound,      // seek: landed on a station
    kNotFound,   // seek: full circle, nothing above threshold
    kCompleted,  // scan: reached the band end
    kCancelled,
    kAborted,    // deadline exceeded
};

struct StepScanEnv {
    // One probe per channel. Returns false when the driver rejected the step
    // (no reading); on true, *valid is the kernel verdict and *rssi_raw the
    // raw CQI signal level (see mtk_ioctl.h for the scale).
    bool (*probe)(uint32_t frequency_khz, int32_t *rssi_raw, int32_t *valid, void *ctx);
    bool (*should_cancel)(void *ctx);
    uint64_t (*now_ms)(void *ctx);
    void *ctx;
};

struct StepScanConfig {
    uint32_t lower_khz;
    uint32_t upper_khz;
    uint32_t start_khz;
    int step_khz;
    // Raw CQI scale (same scale the probe returns): a channel must be
    // kernel-valid AND at or above this level to count.
    int32_t threshold_raw;
    uint64_t deadline_ms;  // absolute, in now_ms() units
};

struct StepScanOutcome {
    StepScanStatus status;
    uint32_t found_frequency_khz = 0;   // kFound only
    uint32_t resume_frequency_khz = 0;  // where the caller should leave the tuner
};

// Runs synchronously; the backend drives it from a worker thread.
// collector may be null for seek modes.
StepScanOutcome step_scan_run(
        StepScanMode mode,
        const StepScanConfig &config,
        const StepScanEnv &env,
        ScanResultCollector *collector);

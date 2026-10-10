#pragma once

// Userspace step-scan state machine for the MTK backend (the driver has no
// hardware seek/scan). Pure logic: the environment (tune/rssi/cancel/clock/
// sleep) is injected, so this is host-testable. The scan collects into the
// shared ScanResultCollector.

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
    bool (*tune)(uint32_t frequency_khz, void *ctx);
    bool (*rssi_dbm)(int32_t *out, void *ctx);
    bool (*should_cancel)(void *ctx);
    uint64_t (*now_ms)(void *ctx);
    void (*sleep_ms)(uint32_t ms, void *ctx);
    void *ctx;
};

struct StepScanConfig {
    uint32_t lower_khz;
    uint32_t upper_khz;
    uint32_t start_khz;
    int step_khz;
    int32_t threshold_dbm;
    uint32_t settle_ms;
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

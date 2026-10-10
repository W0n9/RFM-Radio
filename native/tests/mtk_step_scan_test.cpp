#include <gtest/gtest.h>

#include <vector>

#include "mtk/step_scan.h"

namespace {

// Scripted fake environment: strong stations at fixed frequencies, weak
// elsewhere; a fake clock; tune/rssi calls recorded.
struct FakeEnv {
    StepScanEnv env;
    std::vector<uint32_t> tuned;
    std::vector<uint32_t> stations;  // kHz with strong signal
    int32_t weak_dbm = -100;
    int32_t strong_dbm = -60;
    uint64_t now = 0;
    bool cancel = false;
    bool fail_tune_at_enabled = false;
    uint32_t fail_tune_at = 0;
};

bool fake_tune(uint32_t freq_khz, void *ctx) {
    FakeEnv *e = static_cast<FakeEnv *>(ctx);
    e->tuned.push_back(freq_khz);
    if (e->fail_tune_at_enabled && freq_khz == e->fail_tune_at) {
        return false;
    }
    return true;
}

bool fake_rssi(int32_t *out, void *ctx) {
    FakeEnv *e = static_cast<FakeEnv *>(ctx);
    const uint32_t cur = e->tuned.empty() ? 0 : e->tuned.back();
    for (uint32_t station : e->stations) {
        if (cur == station) {
            *out = e->strong_dbm;
            return true;
        }
    }
    *out = e->weak_dbm;
    return true;
}

bool fake_cancel(void *ctx) {
    return static_cast<FakeEnv *>(ctx)->cancel;
}

uint64_t fake_now(void *ctx) {
    return static_cast<FakeEnv *>(ctx)->now;
}

void fake_sleep(uint32_t ms, void *ctx) {
    static_cast<FakeEnv *>(ctx)->now += ms;
}

StepScanEnv make_env(FakeEnv *e) {
    return StepScanEnv{fake_tune, fake_rssi, fake_cancel, fake_now, fake_sleep, e};
}

StepScanConfig make_config() {
    return StepScanConfig{
            .lower_khz = 87500,
            .upper_khz = 108000,
            .start_khz = 90000,
            .step_khz = 100,
            .threshold_dbm = -74,
            .settle_ms = 60,
            .deadline_ms = 100000,
    };
}

TEST(StepScanTest, FullScanCollectsStationsInOrder) {
    FakeEnv e;
    e.stations = {91200, 99600, 87500};
    ScanResultCollector collector;

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kFullScan, make_config(), make_env(&e), &collector);

    EXPECT_EQ(outcome.status, StepScanStatus::kCompleted);
    EXPECT_EQ(outcome.resume_frequency_khz, 90000u);
    ScanResult result;
    collector.copy_sorted(&result);
    ASSERT_EQ(result.count, 3);
    EXPECT_EQ(result.frequencies_khz[0], 87500);
    EXPECT_EQ(result.frequencies_khz[1], 91200);
    EXPECT_EQ(result.frequencies_khz[2], 99600);
}

TEST(StepScanTest, FullScanVisitsEveryChannelExactlyOnce) {
    FakeEnv e;
    ScanResultCollector collector;
    StepScanConfig config = make_config();
    config.step_khz = 200;

    step_scan_run(StepScanMode::kFullScan, config, make_env(&e), &collector);

    // 87500..108000 by 200 -> 103 channels, last on-grid is 107900
    ASSERT_EQ(e.tuned.size(), 103u);
    EXPECT_EQ(e.tuned.front(), 87500u);
    EXPECT_EQ(e.tuned.back(), 107900u);
}

TEST(StepScanTest, FullScanCancelStopsWithoutCompleting) {
    FakeEnv e;
    ScanResultCollector collector;
    e.stations = {88000, 90000};

    // Cancel after the third tune.
    struct CancelCtx {
        FakeEnv *env;
    } cc{&e};

    StepScanEnv env = make_env(&e);
    // Wrap cancel: trigger after N tunes via the tune hook.
    static int tune_count = 0;
    tune_count = 0;
    env.tune = [](uint32_t f, void *ctx) {
        FakeEnv *ee = static_cast<FakeEnv *>(ctx);
        if (++tune_count == 3) {
            ee->cancel = true;
        }
        return fake_tune(f, ctx);
    };

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kFullScan, make_config(), env, &collector);

    EXPECT_EQ(outcome.status, StepScanStatus::kCancelled);
    EXPECT_EQ(outcome.resume_frequency_khz, 90000u);
    EXPECT_LT(e.tuned.size(), 100u);  // stopped early, never reached the end
}

TEST(StepScanTest, FullScanDeadlineAborts) {
    FakeEnv e;
    ScanResultCollector collector;
    StepScanConfig config = make_config();
    config.deadline_ms = 500;  // settle is 60ms per step -> a few steps only

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kFullScan, config, make_env(&e), &collector);

    EXPECT_EQ(outcome.status, StepScanStatus::kAborted);
    EXPECT_LT(e.tuned.size(), 20u);
}

TEST(StepScanTest, FullScanSkipsTuneFailures) {
    FakeEnv e;
    e.stations = {87600};
    e.fail_tune_at_enabled = true;
    e.fail_tune_at = 87700;
    ScanResultCollector collector;
    StepScanConfig config = make_config();
    config.step_khz = 100;
    config.upper_khz = 88000;  // keep the walk short

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kFullScan, config, make_env(&e), &collector);

    EXPECT_EQ(outcome.status, StepScanStatus::kCompleted);
    ScanResult result;
    collector.copy_sorted(&result);
    ASSERT_EQ(result.count, 1);
    EXPECT_EQ(result.frequencies_khz[0], 87600);
}

TEST(StepScanTest, SeekUpFindsNextStation) {
    FakeEnv e;
    e.stations = {99600, 91300};

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kSeekUp, make_config(), make_env(&e), nullptr);

    EXPECT_EQ(outcome.status, StepScanStatus::kFound);
    EXPECT_EQ(outcome.found_frequency_khz, 91300u);  // first above 90000
    EXPECT_EQ(outcome.resume_frequency_khz, 91300u);
}

TEST(StepScanTest, SeekDownFindsPreviousStation) {
    FakeEnv e;
    e.stations = {88700, 99600};

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kSeekDown, make_config(), make_env(&e), nullptr);

    EXPECT_EQ(outcome.status, StepScanStatus::kFound);
    EXPECT_EQ(outcome.found_frequency_khz, 88700u);
}

TEST(StepScanTest, SeekUpWrapsAroundBandEdge) {
    FakeEnv e;
    e.stations = {87600};  // below start; only reachable by wrapping

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kSeekUp, make_config(), make_env(&e), nullptr);

    EXPECT_EQ(outcome.status, StepScanStatus::kFound);
    EXPECT_EQ(outcome.found_frequency_khz, 87600u);
    // Must have wrapped: visited the upper bound before landing on 87600.
    bool hit_upper = false;
    for (uint32_t f : e.tuned) {
        if (f == 108000) hit_upper = true;
    }
    EXPECT_TRUE(hit_upper);
}

TEST(StepScanTest, SeekFullCircleWithoutStationReturnsToStart) {
    FakeEnv e;  // no stations

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kSeekUp, make_config(), make_env(&e), nullptr);

    EXPECT_EQ(outcome.status, StepScanStatus::kNotFound);
    EXPECT_EQ(outcome.resume_frequency_khz, 90000u);
    // Full circle: visited every channel once.
    EXPECT_EQ(e.tuned.size(), 205u);
}

TEST(StepScanTest, SeekHonorsCancel) {
    FakeEnv e;
    e.cancel = true;  // already cancelled before starting

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kSeekUp, make_config(), make_env(&e), nullptr);

    EXPECT_EQ(outcome.status, StepScanStatus::kCancelled);
    EXPECT_TRUE(e.tuned.empty());
}

}  // namespace

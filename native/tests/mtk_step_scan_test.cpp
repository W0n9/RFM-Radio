#include <gtest/gtest.h>

#include <vector>

#include "mtk/step_scan.h"

namespace {

// Scripted fake environment: strong stations at fixed frequencies, weak
// (kernel-invalid) elsewhere; a fake clock advanced by each probe (each
// SOFT_MUTE_TUNE costs real firmware time); probe calls recorded.
struct FakeEnv {
    StepScanEnv env;
    std::vector<uint32_t> probed;
    std::vector<uint32_t> stations;  // kHz, valid + strong signal
    std::vector<std::pair<uint32_t, int32_t>> graded;  // per-frequency raw rssi (valid), wins over stations
    std::vector<uint32_t> loud_invalid;  // strong raw level but kernel verdict = 0
    int32_t strong_raw = -100;
    uint64_t now = 0;
    bool cancel = false;
    int cancel_after_n_probes = 0;  // 0 = never; cancelled once N probes have run
    bool fail_probe_at_enabled = false;
    uint32_t fail_probe_at = 0;
    int probes = 0;
};

bool fake_probe(uint32_t freq_khz, int32_t *rssi, int32_t *valid, void *ctx) {
    FakeEnv *e = static_cast<FakeEnv *>(ctx);
    e->probed.push_back(freq_khz);
    e->probes++;
    e->now += 100;
    if (e->fail_probe_at_enabled && freq_khz == e->fail_probe_at) {
        return false;
    }
    if (e->cancel_after_n_probes > 0 && e->probes >= e->cancel_after_n_probes) {
        e->cancel = true;
    }
    for (const auto &g : e->graded) {
        if (freq_khz == g.first) {
            *rssi = g.second;
            *valid = 1;
            return true;
        }
    }
    for (uint32_t station : e->stations) {
        if (freq_khz == station) {
            *rssi = e->strong_raw;
            *valid = 1;
            return true;
        }
    }
    for (uint32_t bad : e->loud_invalid) {
        if (freq_khz == bad) {
            *rssi = -50;
            *valid = 0;
            return true;
        }
    }
    *rssi = -300;
    *valid = 0;
    return true;
}

bool fake_cancel(void *ctx) {
    return static_cast<FakeEnv *>(ctx)->cancel;
}

uint64_t fake_now(void *ctx) {
    return static_cast<FakeEnv *>(ctx)->now;
}

StepScanEnv make_env(FakeEnv *e) {
    return StepScanEnv{fake_probe, fake_cancel, fake_now, e};
}

StepScanConfig make_config() {
    return StepScanConfig{
            .lower_khz = 87500,
            .upper_khz = 108000,
            .start_khz = 90000,
            .step_khz = 100,
            .threshold_raw = -235,
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
    ASSERT_EQ(e.probed.size(), 103u);
    EXPECT_EQ(e.probed.front(), 87500u);
    EXPECT_EQ(e.probed.back(), 107900u);
}

TEST(StepScanTest, FullScanCancelStopsWithoutCompleting) {
    FakeEnv e;
    e.stations = {88000, 90000};
    e.cancel_after_n_probes = 3;

    ScanResultCollector collector;
    const StepScanOutcome outcome = step_scan_run(StepScanMode::kFullScan, make_config(), make_env(&e), &collector);

    EXPECT_EQ(outcome.status, StepScanStatus::kCancelled);
    EXPECT_EQ(outcome.resume_frequency_khz, 90000u);
    EXPECT_LT(e.probed.size(), 100u);  // stopped early, never reached the end
}

TEST(StepScanTest, FullScanDeadlineAborts) {
    FakeEnv e;
    ScanResultCollector collector;
    StepScanConfig config = make_config();
    config.deadline_ms = 500;  // each probe costs 100 fake ms -> a few steps only

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kFullScan, config, make_env(&e), &collector);

    EXPECT_EQ(outcome.status, StepScanStatus::kAborted);
    EXPECT_LT(e.probed.size(), 20u);
}

TEST(StepScanTest, FullScanSkipsProbeFailures) {
    FakeEnv e;
    e.stations = {87600};
    e.fail_probe_at_enabled = true;
    e.fail_probe_at = 87700;
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

TEST(StepScanTest, FullScanIgnoresKernelInvalidChannels) {
    FakeEnv e;
    // A loud but kernel-invalid channel (e.g. PAMD/ATDC garbage) must not list
    // even though its raw level dwarfs the threshold.
    e.loud_invalid = {99600};
    ScanResultCollector collector;
    StepScanConfig config = make_config();
    config.lower_khz = 99400;
    config.upper_khz = 99800;

    step_scan_run(StepScanMode::kFullScan, config, make_env(&e), &collector);

    ScanResult result;
    collector.copy_sorted(&result);
    EXPECT_EQ(result.count, 0);
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
    for (uint32_t f : e.probed) {
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
    EXPECT_EQ(e.probed.size(), 205u);
}

TEST(StepScanTest, ScanMergesAdjacentChannelsToLocalPeak) {
    FakeEnv e;
    // One real station bleeding onto neighbors: only the peak lists.
    e.graded = {{99500, -90}, {99600, -60}, {99700, -70}, {99800, -90}};
    ScanResultCollector collector;
    StepScanConfig config = make_config();
    config.lower_khz = 99400;
    config.upper_khz = 99900;

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kFullScan, config, make_env(&e), &collector);

    EXPECT_EQ(outcome.status, StepScanStatus::kCompleted);
    ScanResult result;
    collector.copy_sorted(&result);
    ASSERT_EQ(result.count, 1);
    EXPECT_EQ(result.frequencies_khz[0], 99600);
}

TEST(StepScanTest, ScanPlateauListsOnce) {
    FakeEnv e;
    e.graded = {{99600, -60}, {99700, -60}};
    ScanResultCollector collector;
    StepScanConfig config = make_config();
    config.lower_khz = 99500;
    config.upper_khz = 99800;

    step_scan_run(StepScanMode::kFullScan, config, make_env(&e), &collector);

    ScanResult result;
    collector.copy_sorted(&result);
    EXPECT_EQ(result.count, 1);  // a flat plateau yields its rightmost channel
}

// Suggestion C: the first hit may be a shoulder of a wider carrier; the seek
// must walk the next channels and land on the strongest one.
TEST(StepScanTest, SeekWalksToPeakNotShoulder) {
    FakeEnv e;
    e.graded = {{91300, -200}, {91400, -100}, {91500, -150}};

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kSeekUp, make_config(), make_env(&e), nullptr);

    EXPECT_EQ(outcome.status, StepScanStatus::kFound);
    EXPECT_EQ(outcome.found_frequency_khz, 91400u);  // 91300 hit, 91400 stronger
    EXPECT_EQ(outcome.resume_frequency_khz, 91400u);
}

TEST(StepScanTest, SeekRefineStopsOnWeakerNeighbor) {
    FakeEnv e;
    e.graded = {{91300, -100}, {91400, -120}};

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kSeekUp, make_config(), make_env(&e), nullptr);

    EXPECT_EQ(outcome.status, StepScanStatus::kFound);
    EXPECT_EQ(outcome.found_frequency_khz, 91300u);
    // Refinement stops at the weaker neighbor: 91500 is never probed.
    EXPECT_EQ(e.probed.back(), 91400u);
}

TEST(StepScanTest, SeekRefineStopsAtBandEdge) {
    FakeEnv e;
    e.stations = {108000};

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kSeekUp, make_config(), make_env(&e), nullptr);

    EXPECT_EQ(outcome.status, StepScanStatus::kFound);
    EXPECT_EQ(outcome.found_frequency_khz, 108000u);
    // Nothing beyond the band may be probed.
    for (uint32_t f : e.probed) {
        EXPECT_LE(f, 108000u);
    }
}

TEST(StepScanTest, SeekRefineHonorsCancel) {
    FakeEnv e;
    e.stations = {91300};
    // 90100..91300 = 13 probes; cancel lands right after the hit, so the
    // first refinement probe must never run.
    e.cancel_after_n_probes = 13;

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kSeekUp, make_config(), make_env(&e), nullptr);

    EXPECT_EQ(outcome.status, StepScanStatus::kCancelled);
    EXPECT_EQ(outcome.resume_frequency_khz, 90000u);
}

TEST(StepScanTest, SeekHonorsCancel) {
    FakeEnv e;
    e.cancel = true;  // already cancelled before starting

    const StepScanOutcome outcome = step_scan_run(StepScanMode::kSeekUp, make_config(), make_env(&e), nullptr);

    EXPECT_EQ(outcome.status, StepScanStatus::kCancelled);
    EXPECT_TRUE(e.probed.empty());
}

}  // namespace

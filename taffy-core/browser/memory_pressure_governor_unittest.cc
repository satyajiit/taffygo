// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/memory_pressure_governor.h"

#include <string>
#include <vector>

#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

// "Memory pressure first stops deltas and optional extraction, then returns a
// recoverable resource error" (protocol section 15).
//
// The order is the assertion. A build that refused observations while still
// running delta streams would be spending memory on an optimization while
// denying the thing the optimization exists to accelerate.

namespace taffy {
namespace {

class RecordingDelegate : public MemoryPressureGovernor::Delegate {
 public:
  void StopDeltaStreamsForMemoryPressure() override {
    events.push_back("stop-deltas");
  }
  void OnDegradationStageChanged(DegradationStage stage) override {
    events.push_back("stage");
    stages.push_back(stage);
  }

  std::vector<std::string> events;
  std::vector<DegradationStage> stages;
};

class MemoryPressureGovernorTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  RecordingDelegate delegate_;
};

TEST_F(MemoryPressureGovernorTest, StartsWithNothingGivenUp) {
  MemoryPressureGovernor governor(&delegate_);
  EXPECT_EQ(governor.stage(), DegradationStage::kNormal);
  EXPECT_FALSE(governor.AdmitObservation().has_value());
  EXPECT_TRUE(governor.AllowsOptionalAdapters());
  EXPECT_EQ(governor.NarrowScope(ObservationScope::kDocument),
            ObservationScope::kDocument);
}

TEST_F(MemoryPressureGovernorTest, StreamsStopBeforeTheStageIsAnnounced) {
  MemoryPressureGovernor governor(&delegate_);
  governor.SetStageForTesting(DegradationStage::kStreamsStopped);

  // The memory the streams were holding is released before anything can
  // observe the new stage. That ordering is the sentence in section 15.
  ASSERT_EQ(delegate_.events.size(), 2u);
  EXPECT_EQ(delegate_.events[0], "stop-deltas");
  EXPECT_EQ(delegate_.events[1], "stage");
}

TEST_F(MemoryPressureGovernorTest, ObservationsSurviveTheFirstStage) {
  MemoryPressureGovernor governor(&delegate_);
  governor.SetStageForTesting(DegradationStage::kStreamsStopped);

  EXPECT_FALSE(governor.AdmitObservation().has_value());
  EXPECT_FALSE(governor.AllowsOptionalAdapters());
  // Narrowed to what a person can actually see, which keeps the observation
  // useful while costing the least.
  EXPECT_EQ(governor.NarrowScope(ObservationScope::kDocument),
            ObservationScope::kViewport);
  // Narrowing is a clamp: a request already narrower is left alone.
  EXPECT_EQ(governor.NarrowScope(ObservationScope::kSelection),
            ObservationScope::kSelection);
}

TEST_F(MemoryPressureGovernorTest, TheSecondStageRefusesRecoverably) {
  MemoryPressureGovernor governor(&delegate_);
  governor.SetStageForTesting(DegradationStage::kObservationsRefused);

  const std::optional<ObservationResultCode> refusal =
      governor.AdmitObservation();
  ASSERT_TRUE(refusal.has_value());
  // Recoverable, and named as such: a transient device condition must not look
  // like a permanent one.
  EXPECT_EQ(*refusal, ObservationResultCode::kResourcePressure);
}

TEST_F(MemoryPressureGovernorTest, ActionsAreNeverStopped) {
  // An in-flight dispatch has either had its capability consumed or is about
  // to. Dropping it would produce the ambiguous outcome the journal exists to
  // avoid, which costs more than the memory it saves.
  EXPECT_TRUE(ActionsAllowedAt(DegradationStage::kNormal));
  EXPECT_TRUE(ActionsAllowedAt(DegradationStage::kStreamsStopped));
  EXPECT_TRUE(ActionsAllowedAt(DegradationStage::kObservationsRefused));
}

TEST_F(MemoryPressureGovernorTest, RecoveryRunsTheLadderBackwards) {
  MemoryPressureGovernor governor(&delegate_);
  governor.SetStageForTesting(DegradationStage::kObservationsRefused);
  governor.SetStageForTesting(DegradationStage::kNormal);

  EXPECT_EQ(governor.stage(), DegradationStage::kNormal);
  EXPECT_FALSE(governor.AdmitObservation().has_value());
  EXPECT_TRUE(governor.AllowsOptionalAdapters());
}

TEST_F(MemoryPressureGovernorTest, TheSameStageTwiceChangesNothing) {
  MemoryPressureGovernor governor(&delegate_);
  governor.SetStageForTesting(DegradationStage::kStreamsStopped);
  const size_t after_first = delegate_.events.size();
  governor.SetStageForTesting(DegradationStage::kStreamsStopped);
  EXPECT_EQ(delegate_.events.size(), after_first);
}

}  // namespace
}  // namespace taffy

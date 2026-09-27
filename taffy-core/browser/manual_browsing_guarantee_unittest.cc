// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/manual_browsing_guarantee.h"

#include <iterator>

#include "taffy/browser/ai_runtime_availability.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

// PAR-AI-BR-001 and CAP-BR-001 through CAP-BR-005.
//
// The order of these tests is the argument: first that the check passes for
// the seams as written, then — and this is the one that makes the first
// meaningful — that the check actually catches a capability that depends on
// the runtime. A guarantee that could not fail would be a guarantee that
// proved nothing.

namespace taffy {
namespace {

// Honest: works the same whatever the runtime is doing.
class IndependentProbe : public ManualCapabilityProbe {
 public:
  ManualCapabilityStatus Probe() override {
    ++calls;
    return status;
  }
  ManualCapabilityStatus status = ManualCapabilityStatus::kOperational;
  int calls = 0;
};

// The bug this whole class exists to catch: a manual-browsing capability that
// asks whether the assistant is ready before doing its job.
class RuntimeDependentProbe : public ManualCapabilityProbe {
 public:
  ManualCapabilityStatus Probe() override {
    return AiRuntimeAvailability::Get().IsAvailable()
               ? ManualCapabilityStatus::kOperational
               : ManualCapabilityStatus::kFailed;
  }
};

class ManualBrowsingGuaranteeTest : public testing::Test {
 protected:
  void TearDown() override {
    // Only capabilities without a built-in probe are used here, so clearing
    // them leaves the built-in set intact for the next test.
    ManualBrowsingGuarantee::Get().Register(ManualCapability::kTabsAndSessions,
                                            nullptr);
    ManualBrowsingGuarantee::Get().Register(ManualCapability::kDownloads,
                                            nullptr);
    ManualBrowsingGuarantee::Get().Register(
        ManualCapability::kAndroidLifecycle, nullptr);
    AiRuntimeAvailability::Get().SetState(AiRuntimeState::kAbsent);
  }

  const ManualCapabilityFinding* FindingFor(
      const ManualBrowsingVerdict& verdict,
      ManualCapability capability) {
    for (const ManualCapabilityFinding& finding : verdict.findings) {
      if (finding.capability == capability) {
        return &finding;
      }
    }
    return nullptr;
  }

  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(ManualBrowsingGuaranteeTest, TheBuiltInSeamsAreIndependent) {
  const ManualBrowsingVerdict verdict = ManualBrowsingGuarantee::Get().Verify();

  EXPECT_TRUE(verdict.manual_browsing_is_independent);
  EXPECT_GE(verdict.registered_capability_count, 5u)
      << "The built-in probes are missing. A verdict over an empty set passes "
         "for the wrong reason.";
  for (const ManualCapabilityFinding& finding : verdict.findings) {
    EXPECT_FALSE(finding.varies_with_runtime_state)
        << "capability " << static_cast<int>(finding.capability);
    EXPECT_FALSE(finding.failed_in_some_state)
        << "capability " << static_cast<int>(finding.capability);
    EXPECT_EQ(finding.with_runtime_ready, finding.with_runtime_absent);
  }
}

TEST_F(ManualBrowsingGuaranteeTest, ARuntimeDependentCapabilityIsCaught) {
  RuntimeDependentProbe dependent;
  ManualBrowsingGuarantee::Get().Register(ManualCapability::kTabsAndSessions,
                                          &dependent);

  const ManualBrowsingVerdict verdict = ManualBrowsingGuarantee::Get().Verify();

  EXPECT_FALSE(verdict.manual_browsing_is_independent);
  const ManualCapabilityFinding* finding =
      FindingFor(verdict, ManualCapability::kTabsAndSessions);
  ASSERT_TRUE(finding);
  EXPECT_TRUE(finding->varies_with_runtime_state);
  EXPECT_TRUE(finding->failed_in_some_state);
  EXPECT_EQ(ManualCapabilityStatus::kOperational, finding->with_runtime_ready);
  EXPECT_EQ(ManualCapabilityStatus::kFailed, finding->with_runtime_absent);
}

TEST_F(ManualBrowsingGuaranteeTest, EveryStateIsExercised) {
  IndependentProbe probe;
  ManualBrowsingGuarantee::Get().Register(ManualCapability::kDownloads, &probe);

  ManualBrowsingGuarantee::Get().Verify();

  // Six states, not two. "Absent" and "ready" are the two that come to mind
  // and the two that break least often.
  EXPECT_EQ(static_cast<int>(std::size(kAllAiRuntimeStates)), probe.calls);
  EXPECT_EQ(6, probe.calls);
}

TEST_F(ManualBrowsingGuaranteeTest, AnUnwiredPlatformIsNotARuntimeDependency) {
  // A missing platform delegate is a deployment problem. It must not be
  // reported as an AI dependency, because the fix is completely different.
  IndependentProbe probe;
  probe.status = ManualCapabilityStatus::kPlatformNotWired;
  ManualBrowsingGuarantee::Get().Register(ManualCapability::kDownloads, &probe);

  const ManualBrowsingVerdict verdict = ManualBrowsingGuarantee::Get().Verify();
  const ManualCapabilityFinding* finding =
      FindingFor(verdict, ManualCapability::kDownloads);
  ASSERT_TRUE(finding);
  EXPECT_FALSE(finding->varies_with_runtime_state);
  EXPECT_FALSE(finding->failed_in_some_state);
  EXPECT_TRUE(verdict.manual_browsing_is_independent);
}

TEST_F(ManualBrowsingGuaranteeTest, VerifyingRestoresTheRuntimeState) {
  // Verify sets the availability six times. Leaving it wherever the loop
  // finished would make running the check a side effect on a live browser.
  AiRuntimeAvailability::Get().SetState(AiRuntimeState::kFailed);

  ManualBrowsingGuarantee::Get().Verify();

  EXPECT_EQ(AiRuntimeState::kFailed, AiRuntimeAvailability::Get().state());
}

TEST_F(ManualBrowsingGuaranteeTest, AvailabilityHasOneDefinition) {
  for (AiRuntimeState state : kAllAiRuntimeStates) {
    AiRuntimeAvailability::Get().SetState(state);
    EXPECT_EQ(state == AiRuntimeState::kReady,
              AiRuntimeAvailability::Get().IsAvailable())
        << "state " << static_cast<int>(state);
    EXPECT_EQ(AiRuntimeIsAvailable(state),
              AiRuntimeAvailability::Get().IsAvailable());
  }
}

TEST_F(ManualBrowsingGuaranteeTest, TheDefaultStateIsAbsent) {
  // A build where the assistant was never wired up reports the same thing as
  // one where it is switched off, and manual browsing is identical in both.
  EXPECT_FALSE(AiRuntimeIsAvailable(AiRuntimeState::kAbsent));
  EXPECT_FALSE(AiRuntimeIsAvailable(AiRuntimeState::kUninitialised));
  EXPECT_FALSE(AiRuntimeIsAvailable(AiRuntimeState::kInitialising));
  EXPECT_FALSE(AiRuntimeIsAvailable(AiRuntimeState::kFailed));
  EXPECT_FALSE(AiRuntimeIsAvailable(AiRuntimeState::kDisabled));
  EXPECT_TRUE(AiRuntimeIsAvailable(AiRuntimeState::kReady));
}

TEST_F(ManualBrowsingGuaranteeTest, UnregisteringRemovesTheCapability) {
  IndependentProbe probe;
  ManualBrowsingGuarantee::Get().Register(ManualCapability::kAndroidLifecycle,
                                          &probe);
  const size_t with = ManualBrowsingGuarantee::Get().registered_count();

  ManualBrowsingGuarantee::Get().Register(ManualCapability::kAndroidLifecycle,
                                          nullptr);
  EXPECT_EQ(with - 1, ManualBrowsingGuarantee::Get().registered_count());
}

}  // namespace
}  // namespace taffy

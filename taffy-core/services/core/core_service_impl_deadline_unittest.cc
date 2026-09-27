// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <utility>

#include "base/functional/bind.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_service_impl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

namespace mojom = core_service::mojom;

// Reads the one-shot scheduler rather than the clock. The contract is whether
// Rust reported work to wake for at all.
class CoreServiceImplDeadlineTestPeer final {
 public:
  static bool IsScheduled(const CoreServiceImpl& impl) {
    return impl.deadline_scheduler_.IsScheduled();
  }

  static uint64_t ScheduledDeadline(const CoreServiceImpl& impl) {
    return impl.deadline_scheduler_.scheduled_deadline_monotonic_ms();
  }
};

namespace {

constexpr uint64_t kGeneration = 3u;

mojom::CoreBootstrapPtr ValidBootstrap() {
  auto bootstrap = mojom::CoreBootstrap::New();
  bootstrap->service_generation = kGeneration;
  bootstrap->generation_capability_entropy.resize(32u);
  for (size_t index = 0; index < 32u; ++index) {
    bootstrap->generation_capability_entropy[index] =
        static_cast<uint8_t>(index + 1u);
  }
  bootstrap->browser_profile_id = "profile-1";
  bootstrap->browser_session_id = "browser-session-1";
  bootstrap->available_account_methods = {
      mojom::AccountAuthMethod::kGoogle, mojom::AccountAuthMethod::kEmailLink,
      mojom::AccountAuthMethod::kGithub, mojom::AccountAuthMethod::kFacebook};
  return bootstrap;
}

// Accepts everything the service asks of the browser, and counts what it was
// told. A sweep that found nothing must say nothing, and this is what proves
// the difference between "the timer ran" and "the timer published".
class AcceptingCoreHost final : public mojom::CoreHost {
 public:
  mojo::PendingRemote<mojom::CoreHost> BindNewPipe() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  int publish_count() const { return publish_count_; }
  int effect_count() const { return effect_count_; }

 private:
  void RegisterCapability(mojom::MintedCapabilityGrantPtr grant,
                          RegisterCapabilityCallback callback) override {
    std::move(callback).Run(mojom::CapabilityRegistrationStatus::kRegistered);
  }
  void RegisterPendingApprovals(
      mojom::CoreStateBrowserBindingsPtr bindings,
      RegisterPendingApprovalsCallback callback) override {
    std::move(callback).Run(
        mojom::PendingApprovalRegistrationStatus::kRegistered);
  }
  void EvaluateTaskPolicy(mojom::TaskPolicyEffectPtr effect,
                          EvaluateTaskPolicyCallback callback) override {
    std::move(callback).Run(mojom::PolicyEvaluationResult::New());
  }
  void ExecuteTaskEffect(mojom::TaskEffectBindingPtr effect,
                         ExecuteTaskEffectCallback callback) override {
    std::move(callback).Run(mojom::TaskEffectCompletion::New());
  }
  void EmitEffect(mojom::EffectEnvelopePtr effect) override { ++effect_count_; }
  void PublishTaskAnswerEvents(
      std::vector<mojom::TaskAnswerEventPtr> events,
      PublishTaskAnswerEventsCallback callback) override {
    std::move(callback).Run(true);
  }
  void PublishState(mojom::CoreStateUpdatePtr update) override {
    ++publish_count_;
  }

  int effect_count_ = 0;
  int publish_count_ = 0;
  mojo::Receiver<mojom::CoreHost> receiver_{this};
};

class CoreServiceImplDeadlineTest : public testing::Test {
 protected:
  CoreServiceImplDeadlineTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME),
        impl_(service_.BindNewPipeAndPassReceiver()) {}

  // Drives the real Initialize handshake, so the sweep is started by the code
  // under test rather than by the test.
  mojom::InitializationStatus InitializeWith(
      mojom::CoreBootstrapPtr bootstrap) {
    mojom::InitializationStatus status =
        mojom::InitializationStatus::kInvalidBootstrap;
    service_->Initialize(std::move(bootstrap), host_.BindNewPipe(),
                         base::BindLambdaForTesting(
                             [&status](mojom::CoreBootstrapResultPtr result) {
                               if (result) {
                                 status = result->status;
                               }
                             }));
    task_environment_.RunUntilIdle();
    return status;
  }

  base::test::TaskEnvironment task_environment_;
  mojo::Remote<mojom::TaffyCoreService> service_;
  AcceptingCoreHost host_;
  CoreServiceImpl impl_;
};

TEST_F(CoreServiceImplDeadlineTest, EmptyScheduleStaysStoppedAcrossShutdown) {
  EXPECT_FALSE(CoreServiceImplDeadlineTestPeer::IsScheduled(impl_));

  ASSERT_EQ(mojom::InitializationStatus::kReady,
            InitializeWith(ValidBootstrap()));
  EXPECT_FALSE(CoreServiceImplDeadlineTestPeer::IsScheduled(impl_));

  bool accepted = false;
  service_->PrepareForShutdown(base::BindLambdaForTesting(
      [&accepted](bool value) { accepted = value; }));
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(accepted);
  EXPECT_FALSE(CoreServiceImplDeadlineTestPeer::IsScheduled(impl_));
}

TEST_F(CoreServiceImplDeadlineTest, ARefusedBootstrapNeverStartsTheSweep) {
  auto bootstrap = ValidBootstrap();
  bootstrap->service_generation = 0u;

  EXPECT_EQ(mojom::InitializationStatus::kInvalidBootstrap,
            InitializeWith(std::move(bootstrap)));
  EXPECT_FALSE(CoreServiceImplDeadlineTestPeer::IsScheduled(impl_));

  task_environment_.FastForwardBy(base::Hours(1));
  EXPECT_FALSE(CoreServiceImplDeadlineTestPeer::IsScheduled(impl_));
}

TEST_F(CoreServiceImplDeadlineTest, AnIdleServiceNeverStartsTheSweep) {
  ASSERT_EQ(mojom::InitializationStatus::kReady,
            InitializeWith(ValidBootstrap()));

  // Initialization has no pending operation and therefore no deadline. A
  // timer left running here wakes an otherwise resting profile forever.
  EXPECT_FALSE(CoreServiceImplDeadlineTestPeer::IsScheduled(impl_));
  EXPECT_EQ(0u, CoreServiceImplDeadlineTestPeer::ScheduledDeadline(impl_));
}

TEST_F(CoreServiceImplDeadlineTest, AdvancingTimeNeverWakesAnIdleService) {
  ASSERT_EQ(mojom::InitializationStatus::kReady,
            InitializeWith(ValidBootstrap()));
  const int publishes = host_.publish_count();
  const int effects = host_.effect_count();

  task_environment_.FastForwardBy(base::Hours(1));

  EXPECT_FALSE(CoreServiceImplDeadlineTestPeer::IsScheduled(impl_));
  EXPECT_EQ(publishes, host_.publish_count());
  EXPECT_EQ(effects, host_.effect_count());
}

}  // namespace
}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/notreached.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_command_validation.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/contracts/core-service/core_service.mojom-test-utils.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kRevision = 22u;

// Only admission is scripted. The manager validates, serializes, dispatches,
// and correlates the reply over a real bound Mojo pipe. Any unrelated core
// operation fails loudly instead of receiving a fabricated success.
class TaskAnswerSession final : public mojom::CoreSessionInterceptorForTesting {
 public:
  void Bind(mojo::PendingReceiver<mojom::CoreSession> receiver) {
    receiver_.reset();
    receiver_.Bind(std::move(receiver));
  }

  void Submit(mojom::CoreServiceCommandPtr command,
              SubmitCallback callback) override {
    const std::string operation_id = command->operation->operation_id;
    commands.push_back(std::move(command));
    std::move(callback).Run(
        mojom::Admission::New(operation_id, mojom::AdmissionStatus::kAccepted));
  }

  std::vector<mojom::CoreServiceCommandPtr> commands;

 private:
  mojom::CoreSession* GetForwardingInterface() override {
    NOTREACHED() << "Unexpected CoreSession operation in task answer test";
  }

  mojo::Receiver<mojom::CoreSession> receiver_{this};
};

class CoreServiceManagerTaskAnswersTest
    : public testing::TestWithParam<mojom::CoreServiceCommandKind> {
 protected:
  void SetUp() override {
    auto tools = std::make_unique<ProfileToolSupervisor>(
        1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
        ProfileToolSupervisor::LocalModelPorts::Unsupported(),
        ProfileToolSupervisor::MediaPorts::Unsupported());
    manager_ = tail_.MakeManager(
        &context_, /*storage_broker=*/nullptr, std::move(tools),
        base::MakeRefCounted<CorePageObservationBroker>(&context_),
        std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
    BindSession();
    PublishTask(1u, kRevision);
  }

  void TearDown() override { manager_->Shutdown(); }

  void BindSession() {
    service_receiver_ =
        CoreServiceManagerTaskEffectTestPeer::BindService(*manager_);
    session_.Bind(CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_));
  }

  void PublishTask(uint64_t sequence, std::optional<uint64_t> revision) {
    auto bindings = mojom::CoreStateBrowserBindings::New();
    bindings->service_generation = manager_->service_generation();
    bindings->state_sequence = sequence;
    if (revision) {
      bindings->task_revisions.push_back(mojom::TaskRevisionBinding::New(
          "task-1", manager_->service_generation(), *revision,
          std::vector<mojom::TaskControlKind>()));
    }
    ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
              CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                  *manager_, std::move(bindings)));
    auto state = mojom::CoreStateUpdate::New();
    state->service_generation = manager_->service_generation();
    state->sequence = sequence;
    state->core_status_schema_version = 1u;
    state->payload = std::vector<uint8_t>{1u};
    CoreServiceManagerTaskEffectTestPeer::Publish(*manager_, std::move(state));
    ASSERT_EQ(sequence,
              CoreServiceManagerTaskEffectTestPeer::PublishedStateSequence(
                  *manager_));
    ASSERT_EQ(revision, manager_->FindTaskRevision("task-1"));
  }

  mojom::CoreServiceCommandPtr Command(uint64_t generation,
                                       uint64_t revision,
                                       const std::string& task_id = "task-1") {
    const auto deadline = (base::TimeTicks::Now() + base::Seconds(30))
                              .since_origin()
                              .InMilliseconds();
    const std::string id = "answer-" + std::to_string(++next_operation_);
    auto command = mojom::CoreServiceCommand::New();
    command->kind = GetParam();
    command->operation = mojom::OperationEnvelope::New(
        id, generation, revision, static_cast<uint64_t>(deadline), id);
    switch (GetParam()) {
      case mojom::CoreServiceCommandKind::kCompleteHandover:
        command->complete_handover = mojom::CompleteHandoverCommand::New(
            task_id, "handover-1", "lease-before", "lease-resumed", 1u,
            "trace-1");
        break;
      case mojom::CoreServiceCommandKind::kExpireHandover:
        command->expire_handover =
            mojom::ExpireHandoverCommand::New(task_id, "handover-1", "trace-1");
        break;
      case mojom::CoreServiceCommandKind::kSupplyUserInput:
        command->supply_user_input = mojom::SupplyUserInputCommand::New(
            task_id, "Use the current page", "trace-1");
        break;
      case mojom::CoreServiceCommandKind::kFollowUp:
        command->follow_up = mojom::FollowUpCommand::New(
            task_id, "What did you find?", "trace-1");
        break;
      default:
        NOTREACHED();
    }
    EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(*command,
                                                      mojom::kMaxCommandBytes));
    return command;
  }

  void ExpectSubmission(mojom::CoreServiceCommandPtr command, bool accepted) {
    const auto expected_command = command.Clone();
    const size_t prior_count = session_.commands.size();
    base::test::TestFuture<mojom::AdmissionPtr> reply;
    manager_->Submit(std::move(command), reply.GetCallback());
    task_environment_.RunUntilIdle();
    ASSERT_TRUE(reply.IsReady());
    const auto result = reply.Take();
    ASSERT_TRUE(result);
    EXPECT_EQ(expected_command->operation->operation_id, result->operation_id);
    EXPECT_EQ(accepted ? mojom::AdmissionStatus::kAccepted
                       : mojom::AdmissionStatus::kInvalidCommand,
              result->status);
    EXPECT_EQ(0u, manager_->pending_admission_count_for_testing());
    ASSERT_EQ(prior_count + (accepted ? 1u : 0u), session_.commands.size());
    if (accepted) {
      EXPECT_TRUE(session_.commands.back()->Equals(*expected_command));
    }
  }

  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext context_;
  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<mojom::TaffyCoreService> service_receiver_;
  TaskAnswerSession session_;
  uint32_t next_operation_ = 0u;
};

TEST_P(CoreServiceManagerTaskAnswersTest,
       ExactPublishedRevisionReachesTheBoundSession) {
  ExpectSubmission(Command(manager_->service_generation(), kRevision), true);
  if (GetParam() == mojom::CoreServiceCommandKind::kFollowUp) {
    for (size_t question_size :
         std::array{size_t{0}, size_t{mojom::kMaxUserInputAnswerBytes} + 1u}) {
      auto malformed = Command(manager_->service_generation(), kRevision);
      malformed->follow_up->question = std::string(question_size, 'q');
      EXPECT_FALSE(IsStructurallyValidCoreServiceCommand(
          *malformed, mojom::kMaxCommandBytes));
      ExpectSubmission(std::move(malformed), false);
    }
    for (const auto kind : {mojom::CoreServiceCommandKind::kFollowUp,
                            mojom::CoreServiceCommandKind::kSupplyUserInput}) {
      auto ambiguous = Command(manager_->service_generation(), kRevision);
      ambiguous->kind = kind;
      ambiguous->supply_user_input = mojom::SupplyUserInputCommand::New(
          "task-1", "Use the current page", "trace-1");
      EXPECT_FALSE(IsStructurallyValidCoreServiceCommand(
          *ambiguous, mojom::kMaxCommandBytes));
      ExpectSubmission(std::move(ambiguous), false);
    }
  }
}

TEST_P(CoreServiceManagerTaskAnswersTest,
       MissingTaskAndNoncurrentRevisionsNeverReachTheSession) {
  const uint64_t generation = manager_->service_generation();
  ExpectSubmission(Command(generation, kRevision, "missing-task"), false);
  for (uint64_t revision :
       std::array{uint64_t{0}, kRevision - 1u, kRevision + 1u}) {
    SCOPED_TRACE(revision);
    ExpectSubmission(Command(generation, revision), false);
  }
}

TEST_P(CoreServiceManagerTaskAnswersTest,
       ReplacementPublicationWithdrawsThePreviousAnswer) {
  PublishTask(2u, kRevision + 1u);
  ExpectSubmission(Command(manager_->service_generation(), kRevision), false);
  ExpectSubmission(Command(manager_->service_generation(), kRevision + 1u),
                   true);
  PublishTask(3u, std::nullopt);
  ExpectSubmission(Command(manager_->service_generation(), kRevision + 1u),
                   false);
}

TEST_P(CoreServiceManagerTaskAnswersTest,
       ZeroStaleAndFutureGenerationsNeverReachTheSession) {
  const uint64_t first_generation = manager_->service_generation();
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager_);
  ASSERT_GT(manager_->service_generation(), first_generation);
  BindSession();
  PublishTask(1u, kRevision);
  const uint64_t generation = manager_->service_generation();
  for (uint64_t stale :
       std::array{uint64_t{0}, first_generation, generation + 1u}) {
    SCOPED_TRACE(stale);
    ExpectSubmission(Command(stale, kRevision), false);
  }
  ExpectSubmission(Command(generation, kRevision), true);
}

INSTANTIATE_TEST_SUITE_P(
    TaskAnswers,
    CoreServiceManagerTaskAnswersTest,
    testing::Values(mojom::CoreServiceCommandKind::kCompleteHandover,
                    mojom::CoreServiceCommandKind::kExpireHandover,
                    mojom::CoreServiceCommandKind::kSupplyUserInput,
                    mojom::CoreServiceCommandKind::kFollowUp));

}  // namespace
}  // namespace taffy

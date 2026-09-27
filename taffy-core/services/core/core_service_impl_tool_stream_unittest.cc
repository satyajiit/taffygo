// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <string>
#include <utility>

#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_service_impl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

namespace mojom = core_service::mojom;

// A chunk that was dropped and a chunk that never arrived are the same silence
// from outside the process. This reads the one number that tells them apart.
class CoreServiceImplToolStreamTestPeer final {
 public:
  static uint64_t Unrouted(const CoreServiceImpl& impl) {
    return impl.unrouted_tool_stream_chunk_count_;
  }
};

namespace {

constexpr uint64_t kGeneration = 4u;

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

// Accepts everything the service asks of the browser and counts what it was
// told, so a chunk that quietly turned into a state publication would show.
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

mojom::ToolStreamChunkPtr StreamChunk(uint64_t generation,
                                      const std::string& job_id) {
  auto chunk = mojom::ToolStreamChunk::New();
  chunk->operation = mojom::OperationEnvelope::New();
  chunk->operation->operation_id = "tool-operation-1";
  chunk->operation->service_generation = generation;
  chunk->operation->deadline_monotonic_ms = 60'000u;
  chunk->operation->idempotency_key = "tool-key-1";
  chunk->effect_id = "effect-1";
  chunk->job_id = job_id;
  chunk->chunk = mojom::ToolOutputChunk::New();
  chunk->chunk->job_id = job_id;
  chunk->chunk->sequence = 1u;
  chunk->chunk->kind = mojom::ToolChunkKind::kTextUtf8;
  chunk->chunk->text = mojom::TextOutputChunk::New();
  chunk->chunk->text->utf8 = {0x70u, 0x61u, 0x72u, 0x74u};
  chunk->chunk->is_final = false;
  return chunk;
}

class CoreServiceImplToolStreamTest : public testing::Test {
 protected:
  CoreServiceImplToolStreamTest()
      : impl_(service_.BindNewPipeAndPassReceiver()) {}

  // Drives the real handshake, so the session under test is one the code under
  // test agreed to open.
  void OpenReadySession() {
    mojom::InitializationStatus status =
        mojom::InitializationStatus::kInvalidBootstrap;
    service_->Initialize(ValidBootstrap(), host_.BindNewPipe(),
                         base::BindLambdaForTesting(
                             [&status](mojom::CoreBootstrapResultPtr result) {
                               if (result) {
                                 status = result->status;
                               }
                             }));
    task_environment_.RunUntilIdle();
    ASSERT_EQ(mojom::InitializationStatus::kReady, status);
    service_->OpenSession("session-1", session_.BindNewPipeAndPassReceiver());
    task_environment_.RunUntilIdle();
    ASSERT_TRUE(session_.is_connected());
  }

  uint64_t Unrouted() const {
    return CoreServiceImplToolStreamTestPeer::Unrouted(impl_);
  }

  base::test::TaskEnvironment task_environment_;
  mojo::Remote<mojom::TaffyCoreService> service_;
  mojo::Remote<mojom::CoreSession> session_;
  AcceptingCoreHost host_;
  CoreServiceImpl impl_;
};

TEST_F(CoreServiceImplToolStreamTest, AChunkForThisGenerationIsCounted) {
  ASSERT_NO_FATAL_FAILURE(OpenReadySession());
  const int publishes = host_.publish_count();

  session_->DeliverToolStreamChunk(StreamChunk(kGeneration, "job-1"));
  task_environment_.RunUntilIdle();

  // It arrived, it was addressed to this generation, and there was nowhere to
  // route it. That is one number rather than one silence.
  EXPECT_EQ(1u, Unrouted());
  // And nothing else happened: no state was published on the strength of a
  // partial answer, and the session the browser opened is still open.
  EXPECT_EQ(publishes, host_.publish_count());
  EXPECT_TRUE(session_.is_connected());
}

TEST_F(CoreServiceImplToolStreamTest, AChunkFromAReplacedGenerationIsDropped) {
  ASSERT_NO_FATAL_FAILURE(OpenReadySession());

  session_->DeliverToolStreamChunk(StreamChunk(kGeneration + 1u, "job-1"));
  session_->DeliverToolStreamChunk(StreamChunk(kGeneration - 1u, "job-1"));
  task_environment_.RunUntilIdle();

  // A chunk minted under a generation this profile has left names an operation
  // this runtime never staged. It is dropped before it is counted, because a
  // stale chunk is a race the browser cannot avoid rather than a defect.
  EXPECT_EQ(0u, Unrouted());
  EXPECT_TRUE(session_.is_connected());
}

TEST_F(CoreServiceImplToolStreamTest, AChunkThatNamesNoJobIsDropped) {
  ASSERT_NO_FATAL_FAILURE(OpenReadySession());

  session_->DeliverToolStreamChunk(StreamChunk(kGeneration, std::string()));
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0u, Unrouted());
  EXPECT_TRUE(session_.is_connected());
}

TEST_F(CoreServiceImplToolStreamTest, AChunkThatNamesNoEffectIsDropped) {
  ASSERT_NO_FATAL_FAILURE(OpenReadySession());
  auto chunk = StreamChunk(kGeneration, "job-1");
  chunk->effect_id.clear();

  session_->DeliverToolStreamChunk(std::move(chunk));
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0u, Unrouted());
  EXPECT_TRUE(session_.is_connected());
}

TEST_F(CoreServiceImplToolStreamTest, AChunkAfterShutdownIsDropped) {
  ASSERT_NO_FATAL_FAILURE(OpenReadySession());
  bool accepted = false;
  service_->PrepareForShutdown(base::BindLambdaForTesting(
      [&accepted](bool value) { accepted = value; }));
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(accepted);

  session_->DeliverToolStreamChunk(StreamChunk(kGeneration, "job-1"));
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0u, Unrouted());
}

}  // namespace
}  // namespace taffy

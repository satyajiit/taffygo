// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_TEST_SUPPORT_H_
#define TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_TEST_SUPPORT_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "taffy/browser/model/profile_model_broker.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::model_broker_test {

namespace mojom = core_service::mojom;

inline constexpr uint64_t kGeneration = 7u;
inline constexpr char kEndpoint[] = "https://provider.taffy.test";
inline constexpr char kTaskId[] = "task-1";
inline constexpr char kEffectId[] = "effect-1";
inline constexpr char kCredential[] = "fixture-provider-credential";

// One subscription access token, in the shape a claim is read out of: three
// dot-separated segments, an unpadded base64url payload, and a nested claim
// under the vendor's own namespace naming the account. The header and
// signature are filler — nothing in this process verifies either, and a
// fixture carrying real ones would suggest something did.
//
// The payload decodes to:
//   {"sub":"u-1","https://api.openai.com/auth":{"chatgpt_account_id":"acct-9"}}
inline constexpr char kSubscriptionToken[] =
    "aGVhZGVy."
    "eyJzdWIiOiJ1LTEiLCJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2"
    "NvdW50X2lkIjoiYWNjdC05In19"
    ".c2ln";
inline constexpr char kWorkerOrigin[] = "https://edge.taffy.test";
inline constexpr char kTransientHandle[] = "AbCdEfGhIjKlMnOpQrStUvWxYz012345";

inline uint64_t NowMs() {
  return static_cast<uint64_t>(
      base::TimeTicks::Now().since_origin().InMilliseconds());
}

inline mojom::ModelStaticHeaderPtr Header(const std::string& name,
                                          const std::string& value) {
  auto header = mojom::ModelStaticHeader::New();
  header->name = name;
  header->value = value;
  return header;
}

inline mojom::EffectEnvelopePtr ModelEffect() {
  auto request = mojom::ModelRequestEffect::New();
  request->route_id = "managed.primary";
  request->model_id = "fixture-model";
  request->disclosure = mojom::DisclosureClass::kUserSelectedContent;
  const std::string body = R"({"messages":[]})";
  request->request_body.assign(body.begin(), body.end());
  request->max_output_bytes = 4096;
  request->task_id = kTaskId;
  request->provider_id = "fixture-provider";
  request->wire_api = mojom::ProviderWireApi::kAnthropicMessages;
  request->endpoint = kEndpoint;
  request->credential_handle = "credential-handle-1";
  request->static_headers.push_back(Header("anthropic-version", "2023-06-01"));

  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = "operation-1";
  effect->operation->service_generation = kGeneration;
  effect->operation->task_revision = 1u;
  effect->operation->deadline_monotonic_ms = NowMs() + 60'000u;
  effect->operation->idempotency_key = "idempotency-1";
  effect->effect_id = kEffectId;
  effect->kind = mojom::EffectKind::kModelRequest;
  // A model call is consequential: it may already have been billed by the time
  // anything here learns what happened to it.
  effect->retry_class = mojom::RetryClass::kConsequential;
  effect->model_request = std::move(request);
  return effect;
}

inline mojom::EffectEnvelopePtr ManagedEffect() {
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->wire_api = mojom::ProviderWireApi::kManaged;
  effect->model_request->endpoint = kWorkerOrigin;
  effect->model_request->credential_handle = std::nullopt;
  effect->model_request->static_headers.clear();
  return effect;
}

inline mojom::EffectEnvelopePtr ProbeEffect(const std::string& handle) {
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->probe = true;
  effect->model_request->task_id = std::string();
  effect->model_request->credential_handle = handle;
  effect->retry_class = mojom::RetryClass::kNever;
  return effect;
}

inline std::string CompletionOf(const mojom::EffectResultPtr& result) {
  return std::string(result->model->completion.begin(),
                     result->model->completion.end());
}

class ProfileModelBrokerTest : public testing::Test {
 protected:
  ProfileModelBrokerTest();
  ~ProfileModelBrokerTest() override;

  void SetUp() override;

  // Phase one, answered however the test wants it answered.
  void InstallResolver() {
    broker_->SetCredentialResolver(base::BindLambdaForTesting(
        [this](const std::string& provider_id, const std::string& handle,
               ProfileModelBroker::ProviderCredentialCallback callback) {
          asked_provider_ = provider_id;
          asked_handle_ = handle;
          if (hold_phase_two_) {
            held_ = std::move(callback);
            return;
          }
          std::move(callback).Run(credential_, credential_origin_);
        }));
  }

  void Start(mojom::EffectEnvelopePtr effect) {
    broker_->Dispatch(
        std::move(effect),
        base::BindLambdaForTesting([this](mojom::EffectResultPtr result) {
          ++terminal_count_;
          terminal_ = std::move(result);
        }));
  }

  const mojom::EffectResultPtr& Run(mojom::EffectEnvelopePtr effect) {
    Start(std::move(effect));
    task_environment_.RunUntilIdle();
    return terminal_;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO,
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  network::TestURLLoaderFactory factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_factory_;
  std::unique_ptr<ProfileModelBroker> broker_;
  bool hold_phase_two_ = false;
  ProfileModelBroker::ProviderCredentialCallback held_;
  std::optional<std::string> credential_ = std::string(kCredential);
  // The address a credential names, for the one vendor shape that names one.
  std::optional<std::string> credential_origin_;
  std::string asked_provider_;
  std::string asked_handle_;
  std::optional<network::ResourceRequest> observed_;
  std::string observed_body_;
  std::string streamed_body_;
  uint32_t next_stream_sequence_ = 0u;
  mojom::ModelStreamChunkStatus stream_status_ =
      mojom::ModelStreamChunkStatus::kAccepted;
  bool hold_stream_chunk_ = false;
  ProfileModelBroker::ModelStreamChunkCallback held_stream_chunk_;
  mojom::EffectResultPtr terminal_;
  int terminal_count_ = 0;
};

inline ProfileModelBrokerTest::ProfileModelBrokerTest() = default;

inline ProfileModelBrokerTest::~ProfileModelBrokerTest() = default;

inline void ProfileModelBrokerTest::SetUp() {
  shared_factory_ =
      base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
          &factory_);
  broker_ = std::make_unique<ProfileModelBroker>(shared_factory_,
                                                 std::string(kWorkerOrigin));
  InstallResolver();
  broker_->SetModelStreamChunkDispatcher(base::BindLambdaForTesting(
      [this](mojom::ModelStreamChunkPtr chunk,
             ProfileModelBroker::ModelStreamChunkCallback callback) {
        ASSERT_TRUE(chunk);
        ASSERT_TRUE(chunk->operation);
        EXPECT_EQ(chunk->operation->service_generation, kGeneration);
        EXPECT_EQ(chunk->effect_id, kEffectId);
        EXPECT_EQ(chunk->sequence, next_stream_sequence_);
        if (stream_status_ == mojom::ModelStreamChunkStatus::kAccepted) {
          streamed_body_.append(chunk->data.begin(), chunk->data.end());
          ++next_stream_sequence_;
        }
        if (hold_stream_chunk_) {
          held_stream_chunk_ = std::move(callback);
          return;
        }
        std::move(callback).Run(stream_status_);
      }));
  factory_.SetInterceptor(base::BindLambdaForTesting(
      [this](const network::ResourceRequest& request) {
        observed_ = request;
        observed_body_ = network::GetUploadData(request);
      }));
}

}  // namespace taffy::model_broker_test

#endif  // TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_TEST_SUPPORT_H_

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_ENDPOINT_TEST_SUPPORT_H_
#define TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_ENDPOINT_TEST_SUPPORT_H_

// The fixture two endpoint suites share, and nothing either of them decides.
//
// Extracted when `profile_model_broker_endpoint_unittest.cc` reached the line
// cap and the file had to be split along a seam. The seam it was split on is
// real: composing a path from the compiled vendor table is one subject, and
// which address a person's own server or a credential's issuer may name is
// another, with different authorities behind them. What they have in common is
// only the plumbing — a broker over a test loader factory, one well-formed
// model-request effect, and a way to ask where the request went — so that is
// what lives here.
//
// Nothing in this header asserts anything. A constant that only one of the two
// suites needs stays in that suite: the licensed-vendor row and the
// cleartext own-server address are about address authority and would read here
// as facts the route table cares about, which it does not.

#include "taffy/browser/model/profile_model_broker.h"

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "taffy/browser/core_model_effect_validation.h"
#include "taffy/browser/providerauth/provider_auth_configuration.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy::model_broker_endpoint_test {

namespace mojom = core_service::mojom;

inline constexpr char kProviderId[] = "my-gateway";
inline constexpr char kWorkerOrigin[] = "https://edge.taffy.test";

inline uint64_t NowMs() {
  return static_cast<uint64_t>(
      base::TimeTicks::Now().since_origin().InMilliseconds());
}

// One effect with no credential handle, which is the ordinary shape for a
// server somebody runs themselves and keeps this suite free of the secure
// store: a provider with no key to send needs no resolver installed, so
// nothing here can accidentally be measuring phase one.
inline mojom::EffectEnvelopePtr Effect(mojom::ProviderWireApi wire_api,
                                const std::string &provider_id,
                                const std::string &endpoint,
                                mojom::ModelEndpointKind endpoint_kind) {
  auto request = mojom::ModelRequestEffect::New();
  request->route_id = "route-1";
  request->model_id = "fixture-model";
  request->disclosure = mojom::DisclosureClass::kUserSelectedContent;
  const std::string body = R"({"messages":[]})";
  request->request_body.assign(body.begin(), body.end());
  request->max_output_bytes = 4096;
  request->provider_id = provider_id;
  request->wire_api = wire_api;
  request->endpoint = endpoint;
  // Stated rather than left to value initialization: a field that says which
  // rule is being claimed must never arrive by default.
  request->endpoint_kind = endpoint_kind;

  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = "operation-1";
  effect->operation->service_generation = 7u;
  effect->operation->task_revision = 1u;
  effect->operation->deadline_monotonic_ms = NowMs() + 60'000u;
  effect->operation->idempotency_key = "idempotency-1";
  effect->effect_id = "effect-1";
  effect->kind = mojom::EffectKind::kModelRequest;
  effect->retry_class = mojom::RetryClass::kConsequential;
  effect->model_request = std::move(request);
  return effect;
}

class ProfileModelBrokerEndpointTest : public testing::Test {
protected:
  ProfileModelBrokerEndpointTest();
  ~ProfileModelBrokerEndpointTest() override;

  void SetUp() override;

  // One register with one row, which is all a byte comparison needs. Installed
  // per case, because half of them are about what happens when the row is a
  // different string or is not there at all.
  void Register(const std::string &provider_id, const std::string &endpoint) {
    broker_->SetRegisteredEndpointLookup(base::BindRepeating(
        [](std::string held_provider, std::string held_endpoint,
           const std::string &asked) -> std::optional<std::string> {
          return asked == held_provider
                     ? std::optional<std::string>(held_endpoint)
                     : std::nullopt;
        },
        provider_id, endpoint));
  }

  // The status one dispatch answered with. A status rather than a reference to
  // the result, because several cases below dispatch more than once and a
  // reference into the member would be reading the second answer while naming
  // the first.
  //
  // A dispatch that never answered is reported as `kInvalidResult`, which no
  // case here expects — so a dropped callback fails rather than passing as
  // whatever the previous one left behind.
  mojom::EffectStatus Run(mojom::EffectEnvelopePtr effect) {
    observed_.reset();
    terminal_.reset();
    broker_->Dispatch(std::move(effect),
                      base::BindLambdaForTesting(
                          [this](mojom::EffectResultPtr result) {
                            terminal_ = std::move(result);
                          }));
    task_environment_.RunUntilIdle();
    return terminal_ ? terminal_->status : mojom::EffectStatus::kInvalidResult;
  }

  // The URL one accepted effect was sent to, or an empty GURL when nothing
  // left this process.
  GURL SentTo(mojom::ProviderWireApi wire_api, const std::string &provider_id,
              const std::string &endpoint,
              mojom::ModelEndpointKind endpoint_kind,
              const std::string &expected) {
    factory_.AddResponse(expected, "{}");
    Run(Effect(wire_api, provider_id, endpoint, endpoint_kind));
    return observed_ ? observed_->url : GURL();
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  network::TestURLLoaderFactory factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_factory_;
  std::unique_ptr<ProfileModelBroker> broker_;
  std::optional<network::ResourceRequest> observed_;
  mojom::EffectResultPtr terminal_;
};

inline ProfileModelBrokerEndpointTest::ProfileModelBrokerEndpointTest() =
    default;

inline ProfileModelBrokerEndpointTest::~ProfileModelBrokerEndpointTest() =
    default;

inline void ProfileModelBrokerEndpointTest::SetUp() {
  shared_factory_ =
      base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
          &factory_);
  broker_ = std::make_unique<ProfileModelBroker>(shared_factory_,
                                                 std::string(kWorkerOrigin));
  factory_.SetInterceptor(base::BindLambdaForTesting(
      [this](const network::ResourceRequest &request) { observed_ = request; }));
}

}  // namespace taffy::model_broker_endpoint_test

#endif  // TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_ENDPOINT_TEST_SUPPORT_H_

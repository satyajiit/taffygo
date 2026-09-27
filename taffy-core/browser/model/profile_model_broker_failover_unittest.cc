// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "taffy/browser/model/profile_model_broker_test_support.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using model_broker_test::CompletionOf;
using model_broker_test::kGeneration;
using model_broker_test::kTaskId;
using model_broker_test::ModelEffect;
using model_broker_test::NowMs;
using model_broker_test::ProfileModelBrokerTest;

TEST_F(ProfileModelBrokerTest,
       AnHttpErrorIsClassifiedWithoutBecomingAnswerText) {
  const struct {
    net::HttpStatusCode http;
    mojom::EffectStatus status;
    mojom::ModelErrorClass error_class;
  } kCases[] = {
      {net::HTTP_UNAUTHORIZED, mojom::EffectStatus::kDenied,
       mojom::ModelErrorClass::kAuth},
      {net::HTTP_TOO_MANY_REQUESTS, mojom::EffectStatus::kDenied,
       mojom::ModelErrorClass::kQuota},
      {net::HTTP_INTERNAL_SERVER_ERROR, mojom::EffectStatus::kUnavailable,
       mojom::ModelErrorClass::kOverloaded},
      {net::HTTP_BAD_REQUEST, mojom::EffectStatus::kInvalidResult,
       mojom::ModelErrorClass::kInvalidRequest},
  };
  for (const auto& expected : kCases) {
    factory_.ClearResponses();
    factory_.AddResponse("https://provider.taffy.test/v1/messages",
                         R"({"error":{"type":"fixture"}})", expected.http);

    const mojom::EffectResultPtr& result = Run(ModelEffect());
    ASSERT_TRUE(result) << expected.http;
    EXPECT_EQ(result->status, expected.status) << expected.http;
    ASSERT_TRUE(result->model);
    ASSERT_TRUE(result->model->failure) << expected.http;
    EXPECT_EQ(result->model->failure->error_class, expected.error_class)
        << expected.http;
    EXPECT_FALSE(result->model->failure->has_retry_after) << expected.http;
    EXPECT_EQ(result->model->failure->retry_after_millis, 0u)
        << expected.http;
    EXPECT_TRUE(CompletionOf(result).empty()) << expected.http;
    EXPECT_TRUE(streamed_body_.empty()) << expected.http;
    terminal_.reset();
  }
}

TEST_F(ProfileModelBrokerTest, RetryAfterCrossesAsADelayFactNotARetryDecision) {
  auto head = network::mojom::URLResponseHead::New();
  head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
      "HTTP/1.1 429 Too Many Requests");
  head->headers->SetHeader("Retry-After", "7");
  factory_.AddResponse(
      GURL("https://provider.taffy.test/v1/messages"), std::move(head),
      R"({"error":{"type":"fixture"}})",
      network::URLLoaderCompletionStatus(net::OK));

  const mojom::EffectResultPtr& result = Run(ModelEffect());

  ASSERT_TRUE(result);
  ASSERT_TRUE(result->model);
  ASSERT_TRUE(result->model->failure);
  EXPECT_EQ(result->status, mojom::EffectStatus::kDenied);
  EXPECT_EQ(result->model->failure->error_class,
            mojom::ModelErrorClass::kQuota);
  EXPECT_TRUE(result->model->failure->has_retry_after);
  EXPECT_EQ(result->model->failure->retry_after_millis, 7'000u);
}

// A server that sends `Retry-After` twice is answered from the first one.
//
// This is the half of the header the normalising accessor got wrong. It
// coalesces repeats into one comma-joined string, so two headers arrived as
// "30, 60" and parsed as neither — no delay at all, and a client that retries
// immediately against a vendor that just asked it not to. `retry-after` is on
// Chromium's non-coalescing list for exactly this reason, and the accessor
// that coalesces `DCHECK`s on every name in that list, which made the same
// line fatal in a DCHECK build rather than merely wrong.
TEST_F(ProfileModelBrokerTest, ARepeatedRetryAfterIsReadFromTheFirstOne) {
  auto head = network::mojom::URLResponseHead::New();
  head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
      "HTTP/1.1 429 Too Many Requests");
  head->headers->AddHeader("Retry-After", "30");
  head->headers->AddHeader("Retry-After", "60");
  factory_.AddResponse(
      GURL("https://provider.taffy.test/v1/messages"), std::move(head),
      R"({"error":{"type":"fixture"}})",
      network::URLLoaderCompletionStatus(net::OK));

  const mojom::EffectResultPtr& result = Run(ModelEffect());

  ASSERT_TRUE(result);
  ASSERT_TRUE(result->model);
  ASSERT_TRUE(result->model->failure);
  EXPECT_TRUE(result->model->failure->has_retry_after);
  EXPECT_EQ(result->model->failure->retry_after_millis, 30'000u)
      << "the first value is the answer, not the join of both";
}

TEST_F(ProfileModelBrokerTest, ATransportFailureAfterDispatchIsOutcomeUnknown) {
  factory_.AddResponse(
      GURL("https://provider.taffy.test/v1/messages"),
      network::mojom::URLResponseHead::New(), "",
      network::URLLoaderCompletionStatus(net::ERR_CONNECTION_REFUSED));

  const mojom::EffectResultPtr& result = Run(ModelEffect());
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kOutcomeUnknown);
  EXPECT_TRUE(result->model->completion.empty());
  EXPECT_FALSE(result->model->failure);
}

TEST_F(ProfileModelBrokerTest, ADelayedRetryCanBeCancelledBeforeItCostsWork) {
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->not_before_monotonic_ms = NowMs() + 1'000u;

  Start(std::move(effect));
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(asked_handle_.empty());
  EXPECT_EQ(factory_.NumPending(), 0);
  EXPECT_EQ(terminal_count_, 0);

  broker_->CancelTask(kTaskId, kGeneration);
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(terminal_);
  EXPECT_EQ(terminal_->status, mojom::EffectStatus::kCancelled);

  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_TRUE(asked_handle_.empty());
  EXPECT_EQ(factory_.NumPending(), 0);
  EXPECT_EQ(terminal_count_, 1);
}

}  // namespace
}  // namespace taffy

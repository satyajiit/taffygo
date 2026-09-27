// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the probe learns from a server, and what it refuses to guess.
//
// Every detection here is dictated as the server's own answer, because that
// is what decision 0096 section 5 says a probe reads: a listing shape, a field
// name, a path that answered. A fake loader factory rather than a real server,
// for the reason the model broker's suite gives — these are answers no real
// server can be asked for on demand, including the one that matters most,
// which is a perfectly good address with nothing loaded behind it.

#include "taffy/browser/model/custom_endpoint_prober.h"

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "taffy/browser/model/custom_endpoint_prober_readings.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

// A base URL with a path and a port, which is the shape this whole record
// exists for.
constexpr char kBase[] = "http://192.168.1.9:11434/v1";
constexpr char kListing[] = "http://192.168.1.9:11434/v1/models";
constexpr char kTags[] = "http://192.168.1.9:11434/api/tags";
constexpr char kProps[] = "http://192.168.1.9:11434/props";

class CustomEndpointProberTest : public testing::Test {
 protected:
  void SetUp() override {
    shared_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &factory_);
    prober_ = std::make_unique<CustomEndpointProber>(shared_factory_);
    factory_.SetInterceptor(base::BindLambdaForTesting(
        [this](const network::ResourceRequest& request) {
          ++request_count_;
          requested_.push_back(request.url);
          last_request_ = request;
        }));
  }

  CustomEndpointProber::Answer Run(
      std::optional<std::string> credential = std::nullopt) {
    CustomEndpointProber::Answer answer;
    prober_->Probe(
        GURL(kBase), std::move(credential),
        base::BindLambdaForTesting(
            [&answer](CustomEndpointProber::Answer given) { answer = given; }));
    task_environment_.RunUntilIdle();
    return answer;
  }

  // Nothing at a path is a 404, which is what an address that is not that
  // runtime actually answers.
  void Absent(const char* url) {
    factory_.AddResponse(url, "", net::HTTP_NOT_FOUND);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  network::TestURLLoaderFactory factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_factory_;
  std::unique_ptr<CustomEndpointProber> prober_;
  std::vector<GURL> requested_;
  std::optional<network::ResourceRequest> last_request_;
  int request_count_ = 0;
};

// The fallback: the server served the OpenAI listing and named no runtime, so
// it is OpenAI-compatible because that is what it demonstrated, not because
// nothing else was tried.
TEST_F(CustomEndpointProberTest, AListingWithNoRuntimeIsOpenAiCompatible) {
  factory_.AddResponse(kListing, R"({"data":[{"id":"a"},{"id":"b"}]})");
  Absent(kTags);
  Absent(kProps);

  const CustomEndpointProber::Answer answer = Run();
  EXPECT_TRUE(answer.reached);
  EXPECT_EQ(answer.server_kind, service::ServerKind::kOpenaiCompatible);
  EXPECT_EQ(answer.model_count, 2u);
}

// vLLM names itself in the listing it just served, so nothing further is
// asked: one request, and the runtime paths are never reached.
TEST_F(CustomEndpointProberTest, AMaxModelLenFieldAnswersVllm) {
  factory_.AddResponse(
      kListing, R"({"data":[{"id":"a","max_model_len":8192},{"id":"b"}]})");

  const CustomEndpointProber::Answer answer = Run();
  EXPECT_TRUE(answer.reached);
  EXPECT_EQ(answer.server_kind, service::ServerKind::kVllm);
  EXPECT_EQ(answer.model_count, 2u);
  EXPECT_EQ(request_count_, 1);
}

TEST_F(CustomEndpointProberTest, ApiTagsAnswersOllama) {
  Absent(kListing);
  factory_.AddResponse(kTags, R"({"models":[{"name":"llama3"}]})");

  const CustomEndpointProber::Answer answer = Run();
  EXPECT_TRUE(answer.reached);
  EXPECT_EQ(answer.server_kind, service::ServerKind::kOllama);
  // The listing said nothing, so the count is the one Ollama gave.
  EXPECT_EQ(answer.model_count, 1u);
  // And it carries no roster: `/api/tags` answers in a shape no request can be
  // routed against, so a list built from it would be a list of models this
  // product could not call.
  EXPECT_TRUE(answer.models.empty());
}

// The half of decision 0096 section 5 that a probe without it gets wrong.
//
// A listing that answered proves the base it was asked at — the string the
// person typed — and the models it named come back with it.
TEST_F(CustomEndpointProberTest, AListingProvesTheBaseItAnsweredAt) {
  factory_.AddResponse(
      kListing,
      R"({"data":[{"id":"qwen3-30b","max_model_len":32768},{"id":"a"}]})");

  const CustomEndpointProber::Answer answer = Run();
  ASSERT_TRUE(answer.reached);
  EXPECT_EQ(answer.proved_base, CustomEndpointProber::ProvedBase::kProbedBase);
  ASSERT_EQ(answer.models.size(), 2u);
  EXPECT_EQ(answer.models[0].model_id, "qwen3-30b");
  EXPECT_EQ(answer.models[0].context_window, 32768u);
  // Absent rather than guessed: this entry stated no window.
  EXPECT_EQ(answer.models[1].model_id, "a");
  EXPECT_EQ(answer.models[1].context_window, 0u);
}

// The other half, and the failure it prevents.
//
// A bare origin answers `/api/tags` and does not answer `/v1/models`. Without
// a proved base the probe would report it reachable, the save would file it,
// and every request would join the operation beneath it and reach
// `http://192.168.1.9:11434/chat/completions`, which no runtime serves.
TEST_F(CustomEndpointProberTest, ANativeAnswerProvesTheOriginsV1Base) {
  Absent(kListing);
  factory_.AddResponse(kTags, R"({"models":[{"name":"llama3"}]})");

  const CustomEndpointProber::Answer answer = Run();
  ASSERT_TRUE(answer.reached);
  EXPECT_EQ(answer.proved_base, CustomEndpointProber::ProvedBase::kOriginV1);
  EXPECT_EQ(CustomEndpointProber::OpenAiBaseAtOrigin(GURL(kBase)),
            GURL("http://192.168.1.9:11434/v1"));
}

// An unreached address proves nothing, and says so rather than proposing the
// address it failed at.
TEST_F(CustomEndpointProberTest, AnUnreachedAddressProvesNoBase) {
  Absent(kListing);
  Absent(kTags);
  Absent(kProps);

  const CustomEndpointProber::Answer answer = Run();
  EXPECT_FALSE(answer.reached);
  EXPECT_EQ(answer.proved_base, CustomEndpointProber::ProvedBase::kNothing);
  EXPECT_TRUE(answer.models.empty());
}

TEST_F(CustomEndpointProberTest, PropsAnswersLlamaCpp) {
  Absent(kListing);
  Absent(kTags);
  factory_.AddResponse(kProps, R"({"total_slots":1,"model_path":"/m.gguf"})");

  const CustomEndpointProber::Answer answer = Run();
  EXPECT_TRUE(answer.reached);
  EXPECT_EQ(answer.server_kind, service::ServerKind::kLlamaCpp);
  // `/props` carries no model list, and a count invented here would be a
  // number no screen could act on.
  EXPECT_EQ(answer.model_count, 0u);
}

// The answer this record names in so many words: the address is right and
// nothing is loaded behind it. Reached, with a count of zero.
TEST_F(CustomEndpointProberTest, ZeroModelsIsAnAnswerAndNotAFailure) {
  factory_.AddResponse(kListing, R"({"data":[]})");
  Absent(kTags);
  Absent(kProps);

  const CustomEndpointProber::Answer answer = Run();
  EXPECT_TRUE(answer.reached);
  EXPECT_EQ(answer.server_kind, service::ServerKind::kOpenaiCompatible);
  EXPECT_EQ(answer.model_count, 0u);
}

// Nothing answered anywhere. The verdict is unreached rather than a runtime
// guessed from the port, which is what a probe that read the address instead
// of the answer would have produced.
TEST_F(CustomEndpointProberTest, AnAddressThatAnswersNothingIsUnreached) {
  Absent(kListing);
  Absent(kTags);
  Absent(kProps);

  const CustomEndpointProber::Answer answer = Run();
  EXPECT_FALSE(answer.reached);
  EXPECT_EQ(answer.model_count, 0u);
  EXPECT_EQ(request_count_, 3);
}

// The base path is kept and the runtime paths are not built under it. A
// listing asked at the origin would be a different server than the one a
// person typed, and a runtime path asked under `/v1` is a path no runtime
// serves.
TEST_F(CustomEndpointProberTest, TheBasePathIsKeptAndTheRuntimePathsAreNot) {
  factory_.AddResponse(kListing, R"({"data":[]})");
  Absent(kTags);
  Absent(kProps);
  Run();

  ASSERT_EQ(requested_.size(), 3u);
  EXPECT_EQ(requested_[0], GURL(kListing));
  EXPECT_EQ(requested_[1], GURL(kTags));
  EXPECT_EQ(requested_[2], GURL(kProps));
}

// The transport settings are the model broker's, and these three are the ones
// a probe of an unknown server on a person's own network depends on.
TEST_F(CustomEndpointProberTest, NothingAboutTheRequestFollowsTheServer) {
  factory_.AddResponse(kListing, R"({"data":[]})");
  Absent(kTags);
  Absent(kProps);
  Run(std::string("fixture-key"));

  ASSERT_TRUE(last_request_);
  EXPECT_EQ(last_request_->method, "GET");
  EXPECT_EQ(last_request_->redirect_mode, network::mojom::RedirectMode::kError);
  EXPECT_EQ(last_request_->credentials_mode,
            network::mojom::CredentialsMode::kOmit);
  EXPECT_EQ(last_request_->headers.GetHeader("Authorization"),
            std::optional<std::string>("Bearer fixture-key"));
  // Not in the URL, where every intermediary on the way would log it.
  EXPECT_EQ(last_request_->url.spec().find("fixture-key"), std::string::npos);
}

// A second question while the first is still open is answered rather than
// queued: a person watching a spinner asked once.
TEST_F(CustomEndpointProberTest, OneProbeAtATime) {
  factory_.AddResponse(kListing, R"({"data":[]})");
  Absent(kTags);
  Absent(kProps);

  CustomEndpointProber::Answer first;
  CustomEndpointProber::Answer second;
  prober_->Probe(
      GURL(kBase), std::nullopt,
      base::BindLambdaForTesting(
          [&first](CustomEndpointProber::Answer given) { first = given; }));
  prober_->Probe(
      GURL(kBase), std::nullopt,
      base::BindLambdaForTesting(
          [&second](CustomEndpointProber::Answer given) { second = given; }));
  EXPECT_FALSE(second.reached);
  EXPECT_TRUE(prober_->probing_for_testing());
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(first.reached);
  EXPECT_FALSE(prober_->probing_for_testing());
}

TEST_F(CustomEndpointProberTest, AnAddressWithNowhereToGoAnswersUnreached) {
  CustomEndpointProber::Answer answer;
  prober_->Probe(
      GURL("not an address"), std::nullopt,
      base::BindLambdaForTesting(
          [&answer](CustomEndpointProber::Answer given) { answer = given; }));
  EXPECT_FALSE(answer.reached);
  EXPECT_EQ(request_count_, 0);
}

// The readings themselves, without a network. A body that is not a listing is
// a different fact from a listing with nothing in it, and only the second is
// an answer about the server's models.
TEST(CustomEndpointProberReadingsTest, ABodyThatIsNotAListingIsNotUnderstood) {
  EXPECT_FALSE(ReadOpenAiListing("").understood);
  EXPECT_FALSE(ReadOpenAiListing("not json").understood);
  EXPECT_FALSE(ReadOpenAiListing(R"({"error":"nope"})").understood);
  EXPECT_FALSE(ReadOpenAiListing(R"({"data":{}})").understood);
  EXPECT_TRUE(ReadOpenAiListing(R"({"data":[]})").understood);

  EXPECT_EQ(ReadOllamaTags(R"({"data":[]})"), std::nullopt);
  EXPECT_EQ(ReadOllamaTags(R"({"models":[]})"), std::optional<uint32_t>(0u));
  EXPECT_FALSE(ReadLlamaCppProps("not json"));
  EXPECT_TRUE(ReadLlamaCppProps("{}"));
}

// A listing longer than a custom provider can carry is reported at the bound
// rather than at its own length: a count above it is a number no later screen
// could act on.
TEST(CustomEndpointProberReadingsTest, TheModelCountIsBoundedByTheContract) {
  std::string body = R"({"data":[)";
  const uint32_t bound = static_cast<uint32_t>(service::kMaxCustomModelEntries);
  for (uint32_t index = 0; index < bound + 5u; ++index) {
    if (index != 0u) {
      body += ",";
    }
    body += "{\"id\":\"m\"}";
  }
  body += "]}";

  const OpenAiListingReading reading = ReadOpenAiListing(body);
  EXPECT_TRUE(reading.understood);
  EXPECT_EQ(reading.model_count, bound);
  // The list stops at the same bound, and the two numbers are read separately
  // rather than one from the other: the count is what the server named and the
  // list is what survived, which is the distinction decision 0098 section 4
  // and decision 0096 section 5 both refuse to collapse.
  EXPECT_EQ(reading.models.size(), static_cast<size_t>(bound));
}

// A row with no usable identity is dropped from the list and left in the
// count. It cannot be carried — a request has no name to send — and inventing
// one would put a row in a person's picker that refuses every call.
TEST(CustomEndpointProberReadingsTest, ARowWithNoIdentityIsDroppedNotCounted) {
  const OpenAiListingReading reading = ReadOpenAiListing(
      R"({"data":[{"id":"a"},{"object":"model"},{"id":""},{"id":3}]})");

  EXPECT_TRUE(reading.understood);
  EXPECT_EQ(reading.model_count, 4u);
  ASSERT_EQ(reading.models.size(), 1u);
  EXPECT_EQ(reading.models[0].model_id, "a");
}

TEST(CustomEndpointProberReadingsTest, ExplicitListingCapabilitiesSurvive) {
  const OpenAiListingReading reading = ReadOpenAiListing(R"({"data":[{
    "id":"local-model","context_length":16384,"max_completion_tokens":2048,
    "supported_parameters":["temperature","tools","reasoning"]
  }]})");
  ASSERT_EQ(reading.models.size(), 1u);
  const CustomModelReading& model = reading.models[0];
  EXPECT_EQ(model.context_window, 16384u);
  EXPECT_EQ(model.max_output_tokens, 2048u);
  EXPECT_TRUE(model.reasoning);
  EXPECT_TRUE(model.tool_calling);
  EXPECT_FALSE(reading.names_vllm);
}

TEST(CustomEndpointProberReadingsTest, OnlyAbsentLimitsUseTopProvider) {
  const OpenAiListingReading reading = ReadOpenAiListing(R"({"data":[{
    "id":"nested","context_length":null,
    "top_provider":{"context_length":16384,"max_completion_tokens":2048}
  },{
    "id":"top","context_length":8192,"max_completion_tokens":1024,
    "top_provider":{"context_length":16384,"max_completion_tokens":2048}
  },{
    "id":"configured","max_model_len":4096,"context_length":8192
  }]})");
  ASSERT_EQ(reading.models.size(), 3u);
  EXPECT_EQ(reading.models[0].context_window, 16384u);
  EXPECT_EQ(reading.models[0].max_output_tokens, 2048u);
  EXPECT_EQ(reading.models[1].context_window, 8192u);
  EXPECT_EQ(reading.models[1].max_output_tokens, 1024u);
  EXPECT_EQ(reading.models[2].context_window, 4096u);
}

TEST(CustomEndpointProberReadingsTest, UnknownCapabilitiesAreNeverInferred) {
  for (const char* fields :
       {R"("tool_calling":true,"reasoning":true)",
        R"("capabilities":["tools","reasoning"])",
        R"("supported_parameters":null)",
        R"("supported_parameters":"tools,reasoning")",
        R"("supported_parameters":{"tools":true,"reasoning":true})",
        R"("supported_parameters":[true,{},"Tools","REASONING"])",
        R"("supported_parameters":["tool_choice","reasoning_effort"])",
        R"("supported_parameters":[])"}) {
    SCOPED_TRACE(fields);
    const OpenAiListingReading reading = ReadOpenAiListing(
        std::string(R"({"data":[{"id":"reasoning-tools-model",)") + fields +
        "}]}");
    ASSERT_EQ(reading.models.size(), 1u);
    EXPECT_FALSE(reading.models[0].reasoning);
    EXPECT_FALSE(reading.models[0].tool_calling);
  }
}

TEST(CustomEndpointProberReadingsTest,
     InvalidLimitsStayUnknownWithoutFallback) {
  for (const char* value :
       {"-1", "0", "1.5", "true", "\"8192\"", "2147483648", "4294967296"}) {
    SCOPED_TRACE(value);
    const OpenAiListingReading reading = ReadOpenAiListing(
        std::string(R"({"data":[{"id":"invalid","context_length":)") + value +
        R"(,"max_completion_tokens":)" + value +
        R"(,"top_provider":{"context_length":8192,"max_completion_tokens":1024}}]})");
    ASSERT_EQ(reading.models.size(), 1u);
    EXPECT_EQ(reading.models[0].context_window, 0u);
    EXPECT_EQ(reading.models[0].max_output_tokens, 0u);
  }
}

}  // namespace
}  // namespace taffy

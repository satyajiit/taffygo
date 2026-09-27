// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_model_effect_validation.h"
#include "taffy/browser/model/profile_model_broker_test_support.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using model_broker_test::CompletionOf;
using model_broker_test::Header;
using model_broker_test::kCredential;
using model_broker_test::kEffectId;
using model_broker_test::kSubscriptionToken;
using model_broker_test::ModelEffect;
using model_broker_test::ProfileModelBrokerTest;

TEST_F(ProfileModelBrokerTest, AnAnsweredTaskCallStreamsTheReplyBackUnread) {
  factory_.AddResponse("https://provider.taffy.test/v1/messages",
                       R"({"content":[]})");

  const mojom::EffectResultPtr& result = Run(ModelEffect());
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  EXPECT_EQ(result->kind, mojom::EffectKind::kModelRequest);
  EXPECT_EQ(result->effect_id, kEffectId);
  ASSERT_TRUE(result->model);
  EXPECT_EQ(result->model->model_id, "fixture-model");
  EXPECT_TRUE(result->model->completion.empty());
  EXPECT_TRUE(result->model->streamed);
  EXPECT_EQ(streamed_body_, R"({"content":[]})");
  // Unread by the browser, and the zeroes say so. The bytes went only through
  // the chunk dispatcher to the decoder in the sandbox that owns the four
  // families' tables.
  EXPECT_EQ(result->model->input_units, 0u);
  EXPECT_EQ(result->model->output_units, 0u);

  // The body is the core's, byte for byte. The broker composes no part of it.
  EXPECT_EQ(observed_body_, R"({"messages":[]})");
}

TEST_F(ProfileModelBrokerTest, TheBrokerAddsThePathTheCoreCouldNotName) {
  factory_.AddResponse("https://provider.taffy.test/v1/messages", "{}");
  Run(ModelEffect());

  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->url, GURL("https://provider.taffy.test/v1/messages"));
  EXPECT_EQ(observed_->method, "POST");
  // Nothing about this request may be steered by a response.
  EXPECT_EQ(observed_->redirect_mode, network::mojom::RedirectMode::kError);
  EXPECT_EQ(observed_->credentials_mode,
            network::mojom::CredentialsMode::kOmit);
}

TEST_F(ProfileModelBrokerTest, TheCredentialTravelsInOneHeaderAndNowhereElse) {
  factory_.AddResponse("https://provider.taffy.test/v1/messages", "{}");
  const mojom::EffectResultPtr& result = Run(ModelEffect());

  EXPECT_EQ(asked_provider_, "fixture-provider");
  EXPECT_EQ(asked_handle_, "credential-handle-1");
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->headers.GetHeader("x-api-key"),
            std::optional<std::string>(kCredential));
  // The header the core supplied survived beside it, unaltered.
  EXPECT_EQ(observed_->headers.GetHeader("anthropic-version"),
            std::optional<std::string>("2023-06-01"));

  // Not in the URL, where every intermediary would log it; not in the body,
  // which the core wrote and could read back; and not in the result, which
  // travels to the core.
  EXPECT_EQ(observed_->url.spec().find(kCredential), std::string::npos);
  EXPECT_EQ(observed_body_.find(kCredential), std::string::npos);
  EXPECT_EQ(CompletionOf(result).find(kCredential), std::string::npos);
}

TEST_F(ProfileModelBrokerTest, EveryFamilyPutsItsCredentialWhereTheCoreCannot) {
  // The pairing this rests on: each family's credential header is a name the
  // contract refuses the core as a static header. Asserting it here is what
  // keeps the two tables from drifting apart — a fifth family whose header
  // slipped past the deny list would be a family whose key the core could
  // write itself, out of a value it would have had to hold to write.
  for (const char* name : {"x-api-key", "authorization", "x-goog-api-key"}) {
    mojom::EffectEnvelopePtr effect = ModelEffect();
    effect->model_request->static_headers.push_back(Header(name, "anything"));
    EXPECT_FALSE(IsValidCoreModelRequest(*effect->model_request,
                                         mojom::kMaxIdentifierBytes,
                                         mojom::kMaxEffectBytes))
        << name;
  }
}

TEST_F(ProfileModelBrokerTest, TheGoogleFamilyNamesTheModelInItsPath) {
  factory_.AddResponse(
      "https://provider.taffy.test/v1beta/models/fixture-model:generateContent",
      "{}");
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->wire_api =
      mojom::ProviderWireApi::kGoogleGenerativeLanguage;

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->headers.GetHeader("x-goog-api-key"),
            std::optional<std::string>(kCredential));
  // The family also accepts the key as a query parameter, and this one does
  // not use it: a query string is written to every intermediary's log.
  EXPECT_FALSE(observed_->url.has_query());
}

TEST_F(ProfileModelBrokerTest, AVendorsOwnBasePathIsJoinedInFrontOfTheFamilys) {
  // Several vendors speak a family's exact shape underneath a base path of
  // their own. The prefix is compiled here rather than served, because a
  // served prefix would be a served document choosing where a request goes.
  factory_.AddResponse("https://provider.taffy.test/openai/v1/chat/completions",
                       "{}");
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->wire_api = mojom::ProviderWireApi::kOpenAiCompletions;
  effect->model_request->provider_id = "groq";

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->url,
            GURL("https://provider.taffy.test/openai/v1/chat/completions"));
}

TEST_F(ProfileModelBrokerTest, AProviderWithNoCompiledPrefixIsUnchanged) {
  // The table is an exception list, not a lookup every provider passes
  // through: a provider absent from it must reach exactly the path its family
  // composed before the table existed.
  factory_.AddResponse("https://provider.taffy.test/v1/chat/completions", "{}");
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->wire_api = mojom::ProviderWireApi::kOpenAiCompletions;
  effect->model_request->provider_id = "cerebras";

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->url,
            GURL("https://provider.taffy.test/v1/chat/completions"));
}

TEST_F(ProfileModelBrokerTest, AModelIdThatWouldReshapeAPathIsRefused) {
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->wire_api =
      mojom::ProviderWireApi::kGoogleGenerativeLanguage;
  effect->model_request->model_id = "../v1/messages";

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kDenied);
  EXPECT_EQ(factory_.NumPending(), 0);
  // Refused before the store was asked. A refusal that had spent a trip to the
  // vault would have taken a secret out for a request that never happened.
  EXPECT_TRUE(asked_handle_.empty());
}

TEST_F(ProfileModelBrokerTest, AnEndpointThatIsNotAnOriginIsRefused) {
  // The one thing the core may name about where a call goes is the host. An
  // endpoint carrying a path is the core choosing a route under a host the
  // person already trusted with a credential.
  for (const char* endpoint : {
           "https://provider.taffy.test/v1/messages",
           "https://provider.taffy.test/",
           "https://provider.taffy.test?key=leak",
           "http://provider.taffy.test",
       }) {
    mojom::EffectEnvelopePtr effect = ModelEffect();
    effect->model_request->endpoint = endpoint;

    const mojom::EffectResultPtr& result = Run(std::move(effect));
    ASSERT_TRUE(result) << endpoint;
    EXPECT_EQ(result->status, mojom::EffectStatus::kDenied) << endpoint;
    EXPECT_EQ(factory_.NumPending(), 0) << endpoint;
    EXPECT_TRUE(asked_handle_.empty()) << endpoint;
    terminal_.reset();
  }
}

TEST_F(ProfileModelBrokerTest,
       NetworkDeliveryWaitsForTheSandboxAcknowledgement) {
  hold_stream_chunk_ = true;
  factory_.AddResponse("https://provider.taffy.test/v1/messages",
                       R"({"content":[{"text":"hello"}]})");

  Start(ModelEffect());
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(held_stream_chunk_);
  EXPECT_FALSE(terminal_);

  hold_stream_chunk_ = false;
  std::move(held_stream_chunk_).Run(mojom::ModelStreamChunkStatus::kAccepted);
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(terminal_);
  EXPECT_EQ(terminal_->status, mojom::EffectStatus::kCompleted);
  EXPECT_TRUE(terminal_->model->streamed);
}

TEST_F(ProfileModelBrokerTest,
       ARejectedChunkCancelsTheRequestAndSettlesItOnce) {
  stream_status_ = mojom::ModelStreamChunkStatus::kInvalid;
  factory_.AddResponse("https://provider.taffy.test/v1/messages",
                       R"({"content":[{"text":"hello"}]})");

  const mojom::EffectResultPtr& result = Run(ModelEffect());
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kInvalidResult);
  EXPECT_EQ(terminal_count_, 1);
  EXPECT_EQ(broker_->in_flight_count_for_testing(), 0u);
}

TEST_F(ProfileModelBrokerTest, ATaskReplyIsSplitAtTheContractChunkBound) {
  std::string body(mojom::kMaxModelStreamChunkBytes + 37u, 'x');
  factory_.AddResponse("https://provider.taffy.test/v1/messages", body);
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->max_output_bytes = static_cast<uint32_t>(body.size());

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  EXPECT_EQ(next_stream_sequence_, 2u);
  EXPECT_EQ(streamed_body_, body);
}

TEST_F(ProfileModelBrokerTest, ATaskReplyCannotExceedItsOwnAllowance) {
  factory_.AddResponse("https://provider.taffy.test/v1/messages",
                       std::string(65u, 'x'));
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->max_output_bytes = 64u;

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kResourceLimit);
  EXPECT_TRUE(streamed_body_.empty());
  EXPECT_EQ(terminal_count_, 1);
}

TEST_F(ProfileModelBrokerTest, ATaskCallWithoutASandboxStreamSeamNeverLeaves) {
  broker_->SetModelStreamChunkDispatcher(
      ProfileModelBroker::ModelStreamChunkDispatcher());

  const mojom::EffectResultPtr& result = Run(ModelEffect());
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kUnavailable);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileModelBrokerTest, NothingLeavesUntilPhaseTwoAnswers) {
  hold_phase_two_ = true;
  factory_.AddResponse("https://provider.taffy.test/v1/messages", "{}");

  Start(ModelEffect());
  task_environment_.RunUntilIdle();
  // The whole point of the two phases: the question has been asked, and the
  // request has not been composed, because the header it needs does not exist
  // yet. A design that assumed a synchronous answer would either be blocking
  // this thread here or would have sent the request without the credential.
  EXPECT_EQ(factory_.NumPending(), 0);
  EXPECT_EQ(terminal_count_, 0);
  EXPECT_EQ(asked_handle_, "credential-handle-1");

  std::move(held_).Run(std::string(kCredential), std::nullopt);
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(terminal_);
  EXPECT_EQ(terminal_->status, mojom::EffectStatus::kCompleted);
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->headers.GetHeader("x-api-key"),
            std::optional<std::string>(kCredential));
}

TEST_F(ProfileModelBrokerTest, AStoreWithNothingFiledDeniesTheCall) {
  credential_ = std::nullopt;

  const mojom::EffectResultPtr& result = Run(ModelEffect());
  ASSERT_TRUE(result);
  // Denied, not unavailable: the store was reached and had nothing usable, and
  // that is a fact about this provider's configuration a person can act on.
  EXPECT_EQ(result->status, mojom::EffectStatus::kDenied);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileModelBrokerTest, ADroppedAnswerBecomesARefusalRatherThanAWait) {
  hold_phase_two_ = true;
  Start(ModelEffect());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(terminal_count_, 0);

  // A resolver that goes away with the question outstanding. The core blocks
  // on this effect's terminal, so an answer that never comes is not a slow
  // call, it is a task that never ends.
  held_.Reset();
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(terminal_);
  EXPECT_EQ(terminal_count_, 1);
  EXPECT_EQ(terminal_->status, mojom::EffectStatus::kDenied);
}

TEST_F(ProfileModelBrokerTest, WithNoResolverInstalledTheSeamIsUnavailable) {
  broker_->SetCredentialResolver(ProfileModelBroker::CredentialResolver());

  const mojom::EffectResultPtr& result = Run(ModelEffect());
  ASSERT_TRUE(result);
  // Unavailable rather than denied. The person may have configured this
  // provider perfectly; what is missing is the browser's way of asking.
  EXPECT_EQ(result->status, mojom::EffectStatus::kUnavailable);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileModelBrokerTest, AProviderWithNoCredentialIsStillReachable) {
  // A model server a person runs themselves has no secret to send. Refusing it
  // would make the one provider that needs no credential the one provider that
  // cannot be reached.
  factory_.AddResponse("https://provider.taffy.test/v1/chat/completions", "{}");
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->wire_api = mojom::ProviderWireApi::kOpenAiCompletions;
  effect->model_request->credential_handle.reset();
  effect->model_request->static_headers.clear();

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  EXPECT_TRUE(asked_handle_.empty());
  ASSERT_TRUE(observed_);
  EXPECT_FALSE(observed_->headers.GetHeader("authorization").has_value());
}

TEST_F(ProfileModelBrokerTest, AHeaderThisTransportComposesIsRefused) {
  // Narrower than the contract's deny list and for a different reason: this is
  // about who builds the request. A content type the core proposed would be
  // overwritten by the body attachment, leaving the core having proposed
  // something that did not happen.
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->static_headers.push_back(
      Header("Content-Type", "text/plain"));

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kDenied);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileModelBrokerTest, AnEffectWithNoModelBodyGetsNoResultOfItsOwn) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->kind = mojom::EffectKind::kModelRequest;

  Start(std::move(effect));
  task_environment_.RunUntilIdle();

  EXPECT_EQ(terminal_count_, 1);
  // Null, on purpose. The effect broker still holds the envelope and makes a
  // terminal from it, which beats one synthesized here from a record missing
  // the fields it would need.
  EXPECT_FALSE(terminal_);
}

TEST_F(ProfileModelBrokerTest, TheSubscriptionFamilySendsItsOwnTwoHeaders) {
  // A subscription credential is issued to a client and the endpoint expects
  // every later request to look like it came from that client (decision 0111
  // section 1). Both names are also refused to the core, so a request cannot
  // arrive carrying a second opinion about who is calling.
  factory_.AddResponse("https://provider.taffy.test/backend-api/codex/responses",
                       "{}");
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->wire_api =
      mojom::ProviderWireApi::kOpenAiCodexResponses;
  effect->model_request->static_headers.clear();

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->headers.GetHeader("openai-beta"),
            std::optional<std::string>("responses=experimental"));
  EXPECT_EQ(observed_->headers.GetHeader("originator"),
            std::optional<std::string>("taffygo"));
}

TEST_F(ProfileModelBrokerTest, AFamilysOwnHeadersAreRefusedToTheCore) {
  // The other half of the pair, and the reason the family table and the
  // transport's own list are asserted together: a name this class composes
  // that slipped off that list would be a name the core could state instead,
  // which for an originator is a claim about which product is calling.
  //
  // Refused rather than overwritten. The overwrite would also be correct on
  // the wire and would leave the core believing it had said something.
  for (const char* name : {"openai-beta", "originator"}) {
    mojom::EffectEnvelopePtr effect = ModelEffect();
    effect->model_request->static_headers.push_back(Header(name, "anything"));

    const mojom::EffectResultPtr& result = Run(std::move(effect));
    ASSERT_TRUE(result) << name;
    EXPECT_EQ(result->status, mojom::EffectStatus::kDenied) << name;
    EXPECT_EQ(factory_.NumPending(), 0) << name;
  }
}

TEST_F(ProfileModelBrokerTest, TheSubscriptionFamilyNamesItsAccountFromTheToken) {
  // The account a subscription belongs to, told to the vendor that issued the
  // credential (decision 0111 section 4). It is read out of the credential in
  // this process, where the credential is, and it is never asked of the core:
  // the core has never seen the token, so a core that named an account would
  // be naming one it could not have read.
  factory_.AddResponse("https://provider.taffy.test/backend-api/codex/responses",
                       "{}");
  credential_ = kSubscriptionToken;
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->wire_api =
      mojom::ProviderWireApi::kOpenAiCodexResponses;
  effect->model_request->static_headers.clear();

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->headers.GetHeader("chatgpt-account-id"),
            std::optional<std::string>("acct-9"));
  // And nowhere else. A token read for one claim must not leave any other
  // part of itself in the request.
  EXPECT_EQ(observed_->url.spec().find("acct-9"), std::string::npos);
  EXPECT_EQ(observed_body_.find("acct-9"), std::string::npos);
}

TEST_F(ProfileModelBrokerTest, ACredentialWithNoClaimNamesNoAccount) {
  // A pasted key on the same family. Not sent empty and not guessed: the
  // vendor answers a request with no account named however it chooses to,
  // and inventing a value would name an account nobody has.
  factory_.AddResponse("https://provider.taffy.test/backend-api/codex/responses",
                       "{}");
  mojom::EffectEnvelopePtr effect = ModelEffect();
  effect->model_request->wire_api =
      mojom::ProviderWireApi::kOpenAiCodexResponses;
  effect->model_request->static_headers.clear();

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  ASSERT_TRUE(observed_);
  EXPECT_FALSE(observed_->headers.GetHeader("chatgpt-account-id").has_value());
}

TEST_F(ProfileModelBrokerTest, NoOtherFamilyNamesAnAccount) {
  // The claim is a route fact, so a family that asks for none reads none —
  // even holding the very credential that carries one.
  factory_.AddResponse("https://provider.taffy.test/v1/messages", "{}");
  credential_ = kSubscriptionToken;

  const mojom::EffectResultPtr& result = Run(ModelEffect());
  ASSERT_TRUE(result);
  ASSERT_TRUE(observed_);
  EXPECT_FALSE(observed_->headers.GetHeader("chatgpt-account-id").has_value());
}

}  // namespace
}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

// What these cover: one task effect crossing the process seam in each
// direction. Outbound, the sandbox's CALL_MODEL plan becomes the Mojo binding
// the browser performs — an address, the authority that named it and so the
// rule the browser owes it, a body the wire writers produced, and a credential
// named only by an opaque secure-store handle.
// Inbound, one terminal completion becomes the record the core reads, which is
// where correlation is decided: a completion carrying another effect family's
// result, or another call's identity, is a message about work this binding
// never asked for, and reading it as this call's answer is the failure these
// tests exist to stop.

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

// One CALL_MODEL binding, exactly as the sandbox composes it: an address the
// merged catalog named, the authority that named it, a body the wire writers
// produced, and a credential named only by its opaque secure-store handle.
bridge::BridgeTaskEffect ModelBinding() {
  bridge::BridgeTaskEffect effect{};
  effect.operation.operation_id = "operation-1";
  effect.operation.service_generation = 1u;
  effect.operation.task_revision = 1u;
  effect.operation.deadline_monotonic_ms = 1000u;
  effect.operation.idempotency_key = "key-1";
  effect.effect_id = "effect-1";
  effect.task_id = "task-1";
  effect.ordinal = 0u;
  effect.kind = 10u;  // CALL_MODEL
  effect.call_id = "call-1";
  effect.route_id = "direct_user_key";
  effect.model_id = "model-1";
  effect.disclosure = 2u;  // USER_SELECTED_CONTENT
  effect.wire_api = 0u;    // ANTHROPIC_MESSAGES
  effect.provider_id = "provider-1";
  effect.endpoint = "https://provider.taffy.test";
  effect.endpoint_kind = 0u;  // CATALOG_ORIGIN
  effect.request_body = {'{', '}'};
  effect.max_output_bytes = 1024u;
  effect.has_credential_handle = true;
  effect.credential_handle = "handle-1";
  return effect;
}

// One completion for `effect`, correlated exactly as the browser correlates
// it: the same operation envelope, effect identity, task and effect kind.
mojom::TaskEffectCompletionPtr ModelCompletion(
    const mojom::TaskEffectBinding& effect,
    mojom::TaskEffectCompletionStatus status) {
  mojom::TaskEffectCompletionPtr completion =
      mojom::TaskEffectCompletion::New();
  completion->operation = effect.operation->Clone();
  completion->effect_id = effect.effect_id;
  completion->task_id = effect.task_id;
  completion->kind = effect.kind;
  completion->status = status;
  return completion;
}

// The provider's answer, as the browser's model broker hands it back: opaque
// bytes it did not read, named by the model that produced them.
mojom::EffectResultPtr ModelReply(const mojom::TaskEffectBinding& effect) {
  mojom::EffectResultPtr result = mojom::EffectResult::New();
  result->operation = effect.operation->Clone();
  result->effect_id = effect.effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kModelRequest;
  mojom::ModelEffectResultPtr model = mojom::ModelEffectResult::New();
  model->model_id = "model-1";
  model->completion = {'{', '}'};
  model->input_units = 11u;
  model->output_units = 7u;
  result->model = std::move(model);
  return result;
}

// --- the model call ---------------------------------------------------------

TEST(RustCoreTaskEffectConversionTest, ValidModelEffectProjects) {
  const std::optional<mojom::TaskEffectBindingPtr> projected =
      core_service_internal::ToMojoTaskEffect(ModelBinding());

  ASSERT_TRUE(projected);
  ASSERT_TRUE(*projected);
  EXPECT_EQ(mojom::TaskReducerEffectKind::kCallModel, (*projected)->kind);
  ASSERT_TRUE((*projected)->model);
  EXPECT_EQ("call-1", (*projected)->model->call_id);
  ASSERT_TRUE((*projected)->model->request);
  const mojom::ModelRequestEffect& request = *(*projected)->model->request;
  EXPECT_EQ("direct_user_key", request.route_id);
  EXPECT_EQ("model-1", request.model_id);
  EXPECT_EQ(mojom::DisclosureClass::kUserSelectedContent, request.disclosure);
  EXPECT_EQ(mojom::ProviderWireApi::kAnthropicMessages, request.wire_api);
  EXPECT_EQ("provider-1", request.provider_id);
  EXPECT_EQ("https://provider.taffy.test", request.endpoint);
  EXPECT_EQ(mojom::ModelEndpointKind::kCatalogOrigin, request.endpoint_kind);
  EXPECT_EQ(1024u, request.max_output_bytes);
  EXPECT_EQ(std::vector<uint8_t>({'{', '}'}), request.request_body);
  // The task the binding names, so a cancellation reaches this call.
  EXPECT_EQ((*projected)->task_id, request.task_id);
  ASSERT_TRUE(request.credential_handle);
  EXPECT_EQ("handle-1", *request.credential_handle);
  // Empty because this binding names none, not because none can be carried.
  EXPECT_TRUE(request.static_headers.empty());
}

// A cxx `Vec<String>` is a `rust::Vec<rust::String>`, which is built by
// pushing rather than by a braced list of C string literals.
rust::Vec<rust::String> Strings(std::initializer_list<const char*> values) {
  rust::Vec<rust::String> out;
  for (const char* value : values) {
    out.push_back(rust::String(value));
  }
  return out;
}

TEST(RustCoreTaskEffectConversionTest, StaticHeadersArePairedInThePlansOrder) {
  bridge::BridgeTaskEffect effect = ModelBinding();
  effect.static_header_names = Strings({"session-id", "x-client-flavor"});
  effect.static_header_values = Strings({"0123456789abcdef", "taffy"});

  const std::optional<mojom::TaskEffectBindingPtr> projected =
      core_service_internal::ToMojoTaskEffect(effect);

  ASSERT_TRUE(projected);
  ASSERT_TRUE(*projected);
  ASSERT_TRUE((*projected)->model);
  const mojom::ModelRequestEffect& request = *(*projected)->model->request;
  ASSERT_EQ(2u, request.static_headers.size());
  EXPECT_EQ("session-id", request.static_headers[0]->name);
  EXPECT_EQ("0123456789abcdef", request.static_headers[0]->value);
  EXPECT_EQ("x-client-flavor", request.static_headers[1]->name);
  EXPECT_EQ("taffy", request.static_headers[1]->value);
}

TEST(RustCoreTaskEffectConversionTest, HeaderNamesAndValuesOfUnequalLengthAreRefused) {
  // Refused rather than paired as far as the shorter list goes. A record whose
  // two lists disagree describes no request, and salvaging the overlap would
  // send a value under a name the plan did not give it — which for a header
  // the browser trusts to be the core's is exactly the wrong repair.
  const std::vector<std::pair<std::vector<const char*>, std::vector<const char*>>>
      mismatched = {
          {{"session-id"}, {}},
          {{}, {"0123456789abcdef"}},
          {{"session-id", "x-client-flavor"}, {"0123456789abcdef"}},
      };
  for (const auto& [names, values] : mismatched) {
    bridge::BridgeTaskEffect effect = ModelBinding();
    for (const char* name : names) {
      effect.static_header_names.push_back(rust::String(name));
    }
    for (const char* value : values) {
      effect.static_header_values.push_back(rust::String(value));
    }

    EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(effect).has_value())
        << names.size() << " name(s) against " << values.size() << " value(s)";
  }
}

TEST(RustCoreTaskEffectConversionTest, AbsentCredentialHandleProjectsNone) {
  bridge::BridgeTaskEffect effect = ModelBinding();
  effect.has_credential_handle = false;
  effect.credential_handle = "";

  const std::optional<mojom::TaskEffectBindingPtr> projected =
      core_service_internal::ToMojoTaskEffect(effect);

  ASSERT_TRUE(projected);
  ASSERT_TRUE((*projected)->model);
  ASSERT_TRUE((*projected)->model->request);
  EXPECT_FALSE((*projected)->model->request->credential_handle);
}

TEST(RustCoreTaskEffectConversionTest, UnknownDisclosureClassIsRefused) {
  bridge::BridgeTaskEffect effect = ModelBinding();
  effect.disclosure = 4u;

  EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(effect));
}

TEST(RustCoreTaskEffectConversionTest, UnknownProviderWireApiIsRefused) {
  bridge::BridgeTaskEffect effect = ModelBinding();
  effect.wire_api = 7u;

  EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(effect));
}

TEST(RustCoreTaskEffectConversionTest, TheManagedWireCrossesTheSeam) {
  bridge::BridgeTaskEffect effect = ModelBinding();
  effect.wire_api = 4u;

  const std::optional<mojom::TaskEffectBindingPtr> projected =
      core_service_internal::ToMojoTaskEffect(effect);
  ASSERT_TRUE(projected);
  ASSERT_TRUE(*projected);
  ASSERT_TRUE((*projected)->model);
  ASSERT_TRUE((*projected)->model->request);
  EXPECT_EQ((*projected)->model->request->wire_api,
            mojom::ProviderWireApi::kManaged);
}

TEST(RustCoreTaskEffectConversionTest, ThePersonsOwnEndpointKindIsCarried) {
  bridge::BridgeTaskEffect effect = ModelBinding();
  effect.endpoint_kind = 1u;  // USER_BASE_URL
  effect.endpoint = "http://localhost:11434/v1";

  const std::optional<mojom::TaskEffectBindingPtr> projected =
      core_service_internal::ToMojoTaskEffect(effect);
  ASSERT_TRUE(projected);
  ASSERT_TRUE(*projected);
  ASSERT_TRUE((*projected)->model);
  ASSERT_TRUE((*projected)->model->request);
  // The claim travels; it is not remade here. A turn composed against a
  // candidate from the person's own layer asks the browser for the register
  // rule, and an address that arrived claiming the catalog's would be judged
  // as an https origin and refused for being exactly what it is.
  EXPECT_EQ(mojom::ModelEndpointKind::kUserBaseUrl,
            (*projected)->model->request->endpoint_kind);
  EXPECT_EQ("http://localhost:11434/v1",
            (*projected)->model->request->endpoint);
}

TEST(RustCoreTaskEffectConversionTest, UnknownEndpointKindIsRefused) {
  bridge::BridgeTaskEffect effect = ModelBinding();
  effect.endpoint_kind = 2u;

  // Refused rather than settled: the only settlement available here is the
  // catalog rule, which is the one direction a person's own address must
  // never be sent in.
  EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(effect));
}

TEST(RustCoreTaskEffectConversionTest, EmptyRequestBodyIsRefused) {
  bridge::BridgeTaskEffect effect = ModelBinding();
  effect.request_body = {};

  EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(effect));
}

TEST(RustCoreTaskEffectConversionTest, UnboundedCompletionIsRefused) {
  bridge::BridgeTaskEffect effect = ModelBinding();
  effect.max_output_bytes = 0u;

  EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(effect));
}

TEST(RustCoreTaskEffectConversionTest, CredentialHandleFlagMustAgree) {
  bridge::BridgeTaskEffect claimed = ModelBinding();
  claimed.credential_handle = "";
  EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(claimed));

  bridge::BridgeTaskEffect unclaimed = ModelBinding();
  unclaimed.has_credential_handle = false;
  EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(unclaimed));
}

TEST(RustCoreTaskEffectConversionTest, MissingCallIdentityIsRefused) {
  bridge::BridgeTaskEffect effect = ModelBinding();
  effect.call_id = "";

  EXPECT_FALSE(core_service_internal::ToMojoTaskEffect(effect));
}

// --- the model reply --------------------------------------------------------

TEST(RustCoreTaskEffectConversionTest, ModelCompletionCarriesTheProviderBytes) {
  const std::optional<mojom::TaskEffectBindingPtr> effect =
      core_service_internal::ToMojoTaskEffect(ModelBinding());
  ASSERT_TRUE(effect);
  ASSERT_TRUE(*effect);
  mojom::TaskEffectCompletionPtr completion = ModelCompletion(
      **effect, mojom::TaskEffectCompletionStatus::kSucceeded);
  completion->effect_result = ModelReply(**effect);

  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskTerminal(**effect, *completion);

  ASSERT_TRUE(terminal);
  EXPECT_TRUE(terminal->has_model_completion);
  EXPECT_EQ("model-1", std::string(terminal->model_completion_model_id));
  EXPECT_EQ(std::vector<uint8_t>({'{', '}'}),
            std::vector<uint8_t>(terminal->model_completion.begin(),
                                 terminal->model_completion.end()));
  // A model terminal never carries an observation. The core refuses one, and
  // the reason it can is that nothing on this path ever sets the flag.
  EXPECT_FALSE(terminal->has_observation);
}

TEST(RustCoreTaskEffectConversionTest,
     DefinitiveModelFailureCarriesOnlyTypedRetryFacts) {
  const std::optional<mojom::TaskEffectBindingPtr> effect =
      core_service_internal::ToMojoTaskEffect(ModelBinding());
  ASSERT_TRUE(effect);
  mojom::TaskEffectCompletionPtr completion = ModelCompletion(
      **effect, mojom::TaskEffectCompletionStatus::kUnavailable);
  completion->effect_result = ModelReply(**effect);
  completion->effect_result->status = mojom::EffectStatus::kUnavailable;
  completion->effect_result->model->completion.clear();
  completion->effect_result->model->failure = mojom::ModelFailure::New(
      mojom::ModelErrorClass::kOverloaded, true, 2500u);

  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskTerminal(**effect, *completion);

  ASSERT_TRUE(terminal);
  EXPECT_FALSE(terminal->has_model_completion);
  EXPECT_TRUE(terminal->has_model_failure);
  EXPECT_EQ(static_cast<uint8_t>(mojom::ModelErrorClass::kOverloaded),
            terminal->model_failure_class);
  EXPECT_TRUE(terminal->has_model_retry_after);
  EXPECT_EQ(2500u, terminal->model_retry_after_millis);
  EXPECT_TRUE(terminal->model_completion.empty());
}

TEST(RustCoreTaskEffectConversionTest,
     OutcomeUnknownCannotWearDefinitiveModelFailureFacts) {
  const std::optional<mojom::TaskEffectBindingPtr> effect =
      core_service_internal::ToMojoTaskEffect(ModelBinding());
  ASSERT_TRUE(effect);
  mojom::TaskEffectCompletionPtr completion = ModelCompletion(
      **effect, mojom::TaskEffectCompletionStatus::kOutcomeUnknown);
  completion->effect_result = ModelReply(**effect);
  completion->effect_result->status = mojom::EffectStatus::kOutcomeUnknown;
  completion->effect_result->model->completion.clear();
  completion->effect_result->model->failure = mojom::ModelFailure::New(
      mojom::ModelErrorClass::kNetwork, false, 0u);

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(**effect, *completion));
}

// The one this branch exists to stop: a completion of the right shape for a
// different effect family, arriving under a model call's identity. Read as the
// provider's answer it would be a reply nobody's provider sent.
TEST(RustCoreTaskEffectConversionTest, ModelCompletionOfAnotherFamilyIsRefused) {
  const std::optional<mojom::TaskEffectBindingPtr> effect =
      core_service_internal::ToMojoTaskEffect(ModelBinding());
  ASSERT_TRUE(effect);
  mojom::TaskEffectCompletionPtr completion = ModelCompletion(
      **effect, mojom::TaskEffectCompletionStatus::kSucceeded);
  completion->effect_result = ModelReply(**effect);
  completion->effect_result->kind = mojom::EffectKind::kPageObservation;

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(**effect, *completion));
}

// A reply correlated to another call of the same task answers a request this
// binding did not make.
TEST(RustCoreTaskEffectConversionTest, ModelCompletionOfAnotherCallIsRefused) {
  const std::optional<mojom::TaskEffectBindingPtr> effect =
      core_service_internal::ToMojoTaskEffect(ModelBinding());
  ASSERT_TRUE(effect);
  mojom::TaskEffectCompletionPtr completion = ModelCompletion(
      **effect, mojom::TaskEffectCompletionStatus::kSucceeded);
  completion->effect_result = ModelReply(**effect);
  completion->effect_result->effect_id = "effect-2";

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(**effect, *completion));
}

// Succeeded and empty crosses with the flag clear rather than being refused:
// the core records it as a gap, and a refusal here would tear the core down
// instead of recording anything.
TEST(RustCoreTaskEffectConversionTest, SucceededModelTerminalMayCarryNoReply) {
  const std::optional<mojom::TaskEffectBindingPtr> effect =
      core_service_internal::ToMojoTaskEffect(ModelBinding());
  ASSERT_TRUE(effect);
  const mojom::TaskEffectCompletionPtr completion = ModelCompletion(
      **effect, mojom::TaskEffectCompletionStatus::kSucceeded);

  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskTerminal(**effect, *completion);

  ASSERT_TRUE(terminal);
  EXPECT_FALSE(terminal->has_model_completion);
  EXPECT_TRUE(terminal->model_completion.empty());
}

// Every non-terminal status is a call that produced no bytes, so a result
// arriving with one is a completion this terminal cannot account for.
TEST(RustCoreTaskEffectConversionTest, UnavailableModelTerminalCarriesNoReply) {
  const std::optional<mojom::TaskEffectBindingPtr> effect =
      core_service_internal::ToMojoTaskEffect(ModelBinding());
  ASSERT_TRUE(effect);
  mojom::TaskEffectCompletionPtr completion = ModelCompletion(
      **effect, mojom::TaskEffectCompletionStatus::kUnavailable);
  completion->effect_result = ModelReply(**effect);

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(**effect, *completion));
}

}  // namespace
}  // namespace taffy

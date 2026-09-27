// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_composer.h"
#include "testing/gtest/include/gtest/gtest.h"

// What these cover: the composer suggestion crossing the process seam in each
// direction (decision 0097). Outbound, the flat bridge record the ordered core
// composed becomes the typed model effect the broker dispatches — task-less,
// header-less, never on the managed wire, never without a credential of the
// person's own, and never claiming a disclosure other than the composer text
// the person selected. Inbound, one dispatch terminal becomes the bytes the
// core reads, and one claimed answer becomes the push a surface draws — where
// an absent suggestion is an absent optional rather than an empty string,
// because a surface handed "" would draw nothing and go on waiting.
//
// Each refusal below is reached with a byte one past the contract's last
// member, so a member appended to the schema does not quietly turn a negative
// test into a positive one.

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

bridge::BridgeComposerOperation Operation() {
  bridge::BridgeComposerOperation operation{};
  operation.operation_id = "operation-1";
  operation.service_generation = 1u;
  operation.task_revision = 0u;
  operation.deadline_monotonic_ms = 1000u;
  operation.idempotency_key = "key-1";
  return operation;
}

bridge::BridgeComposerEffect ComposerEffect() {
  bridge::BridgeComposerEffect effect{};
  effect.operation = Operation();
  effect.effect_id = "composer-completion-1";
  effect.retry_class = 2u;  // NEVER
  effect.route_id = "composer";
  effect.model_id = "model-1";
  effect.disclosure = 2u;  // USER_SELECTED_CONTENT
  effect.request_body = {'{', '}'};
  effect.max_output_bytes = 512u;
  effect.provider_id = "provider-1";
  effect.wire_api = 0u;  // ANTHROPIC_MESSAGES
  effect.endpoint = "https://provider.taffy.test";
  effect.credential_handle = "provider-1";
  effect.endpoint_kind = 0u;  // CATALOG_ORIGIN
  return effect;
}

bridge::BridgeComposerDelivery ComposerDelivery() {
  bridge::BridgeComposerDelivery delivery{};
  delivery.claimed = true;
  delivery.operation = Operation();
  delivery.effect_id = "composer-push-request-1";
  delivery.retry_class = 2u;  // NEVER
  delivery.request_id = "request-1";
  delivery.has_text = true;
  delivery.text = " two flights.";
  return delivery;
}

mojom::CoreServiceCommandPtr ComposerCommand() {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New();
  command->operation->operation_id = "operation-1";
  command->operation->service_generation = 1u;
  command->operation->deadline_monotonic_ms = 1000u;
  command->operation->idempotency_key = "key-1";
  command->kind = mojom::CoreServiceCommandKind::kRequestComposerCompletion;
  command->request_composer_completion =
      mojom::RequestComposerCompletionCommand::New();
  command->request_composer_completion->request_id = "request-1";
  command->request_composer_completion->prefix = "Compare the ";
  return command;
}

TEST(RustCoreComposerConversionTest, ValidComposerEffectProjects) {
  const mojom::EffectEnvelopePtr projected =
      core_service_internal::ToMojoComposerEffect(ComposerEffect());
  ASSERT_TRUE(projected);
  EXPECT_EQ(projected->effect_id, "composer-completion-1");
  EXPECT_EQ(projected->kind, mojom::EffectKind::kModelRequest);
  EXPECT_EQ(projected->retry_class, mojom::RetryClass::kNever);
  ASSERT_TRUE(projected->operation);
  EXPECT_EQ(projected->operation->operation_id, "operation-1");
  ASSERT_TRUE(projected->model_request);
  const mojom::ModelRequestEffect &request = *projected->model_request;
  EXPECT_EQ(request.route_id, "composer");
  EXPECT_EQ(request.model_id, "model-1");
  EXPECT_EQ(request.disclosure, mojom::DisclosureClass::kUserSelectedContent);
  EXPECT_EQ(request.wire_api, mojom::ProviderWireApi::kAnthropicMessages);
  EXPECT_EQ(request.endpoint, "https://provider.taffy.test");
  EXPECT_EQ(request.endpoint_kind, mojom::ModelEndpointKind::kCatalogOrigin);
  EXPECT_EQ(request.credential_handle,
            std::optional<std::string>("provider-1"));
  // No task owns a suggestion, and it is not the probe; both are set here
  // rather than copied, because this channel carries nothing else.
  EXPECT_FALSE(request.probe);
  EXPECT_TRUE(request.task_id.empty());
  EXPECT_TRUE(request.static_headers.empty());
}

TEST(RustCoreComposerConversionTest, ThePersonsOwnEndpointKindIsCarried) {
  bridge::BridgeComposerEffect effect = ComposerEffect();
  effect.endpoint_kind = 1u;  // USER_BASE_URL
  const mojom::EffectEnvelopePtr projected =
      core_service_internal::ToMojoComposerEffect(effect);
  ASSERT_TRUE(projected);
  // The claim is the core's: a suggestion may be spent on a provider the
  // person defined, and the browser revalidates that address against its
  // register of what the person typed rather than against the catalog.
  EXPECT_EQ(projected->model_request->endpoint_kind,
            mojom::ModelEndpointKind::kUserBaseUrl);
}

TEST(RustCoreComposerConversionTest, TheManagedWireIsRefused) {
  bridge::BridgeComposerEffect effect = ComposerEffect();
  effect.wire_api = 4u;  // MANAGED
  // A suggestion is never spent from a granted allowance (decision 0097
  // section 2): it is text the person did not send.
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(effect));
}

TEST(RustCoreComposerConversionTest, ARequestWithNoCredentialIsRefused) {
  bridge::BridgeComposerEffect effect = ComposerEffect();
  effect.credential_handle = "";
  // The managed route holds no stored credential, so a request carrying none
  // is the route decision 0097 refuses, whatever wire it names.
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(effect));
}

TEST(RustCoreComposerConversionTest, ADisclosureOtherThanTheComposerIsRefused) {
  bridge::BridgeComposerEffect content_free = ComposerEffect();
  content_free.disclosure = 0u;  // CONTENT_FREE
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(content_free));

  bridge::BridgeComposerEffect page = ComposerEffect();
  page.disclosure = 3u;  // PAGE_CONTENT
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(page));
}

TEST(RustCoreComposerConversionTest, UnknownEnumerationBytesAreRefused) {
  bridge::BridgeComposerEffect wire_api = ComposerEffect();
  wire_api.wire_api = 7u;
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(wire_api));

  bridge::BridgeComposerEffect retry = ComposerEffect();
  retry.retry_class = 3u;
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(retry));

  bridge::BridgeComposerEffect disclosure = ComposerEffect();
  disclosure.disclosure = 4u;
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(disclosure));

  bridge::BridgeComposerEffect endpoint_kind = ComposerEffect();
  endpoint_kind.endpoint_kind = 2u;
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(endpoint_kind));
}

TEST(RustCoreComposerConversionTest, AnEmptyOrUnboundedBodyIsRefused) {
  bridge::BridgeComposerEffect empty = ComposerEffect();
  empty.request_body.clear();
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(empty));

  bridge::BridgeComposerEffect unbounded = ComposerEffect();
  unbounded.max_output_bytes = 0u;
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(unbounded));

  bridge::BridgeComposerEffect oversized = ComposerEffect();
  oversized.max_output_bytes =
      static_cast<uint32_t>(mojom::kMaxComposerCompletionBytes) + 1u;
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(oversized));
}

TEST(RustCoreComposerConversionTest, AnAbsentSuffixIsNotAnEmptyOne) {
  const std::optional<bridge::BridgeComposerCommand> at_the_end =
      core_service_internal::ToBridgeComposerCommand(*ComposerCommand());
  ASSERT_TRUE(at_the_end);
  // A caret at the end of the text is not a caret before an empty string, and
  // the flag is what keeps them apart across a bridge with no optional.
  EXPECT_FALSE(at_the_end->has_suffix);
  EXPECT_TRUE(at_the_end->suffix.empty());

  mojom::CoreServiceCommandPtr command = ComposerCommand();
  command->request_composer_completion->suffix = std::string();
  const std::optional<bridge::BridgeComposerCommand> before_nothing =
      core_service_internal::ToBridgeComposerCommand(*command);
  ASSERT_TRUE(before_nothing);
  EXPECT_TRUE(before_nothing->has_suffix);
  EXPECT_TRUE(before_nothing->suffix.empty());

  mojom::CoreServiceCommandPtr with_text = ComposerCommand();
  with_text->request_composer_completion->suffix = " today.";
  const std::optional<bridge::BridgeComposerCommand> carried =
      core_service_internal::ToBridgeComposerCommand(*with_text);
  ASSERT_TRUE(carried);
  EXPECT_TRUE(carried->has_suffix);
  EXPECT_EQ(std::string(carried->suffix), " today.");
}

TEST(RustCoreComposerConversionTest, ACommandBeyondItsBoundsIsRefused) {
  mojom::CoreServiceCommandPtr no_request = ComposerCommand();
  no_request->request_composer_completion->request_id = "";
  EXPECT_FALSE(core_service_internal::ToBridgeComposerCommand(*no_request));

  mojom::CoreServiceCommandPtr no_prefix = ComposerCommand();
  no_prefix->request_composer_completion->prefix = "";
  EXPECT_FALSE(core_service_internal::ToBridgeComposerCommand(*no_prefix));

  mojom::CoreServiceCommandPtr long_prefix = ComposerCommand();
  long_prefix->request_composer_completion->prefix =
      std::string(mojom::kMaxComposerPrefixBytes + 1u, 'a');
  EXPECT_FALSE(core_service_internal::ToBridgeComposerCommand(*long_prefix));

  mojom::CoreServiceCommandPtr long_suffix = ComposerCommand();
  long_suffix->request_composer_completion->suffix =
      std::string(mojom::kMaxComposerSuffixBytes + 1u, 'a');
  EXPECT_FALSE(core_service_internal::ToBridgeComposerCommand(*long_suffix));

  mojom::CoreServiceCommandPtr other_kind = ComposerCommand();
  other_kind->kind = mojom::CoreServiceCommandKind::kProbeProviderCredential;
  EXPECT_FALSE(core_service_internal::ToBridgeComposerCommand(*other_kind));
}

TEST(RustCoreComposerConversionTest, ACompletionTerminalCarriesTheReplyBody) {
  auto result = mojom::EffectResult::New();
  result->operation = mojom::OperationEnvelope::New();
  result->operation->operation_id = "operation-1";
  result->operation->service_generation = 1u;
  result->operation->deadline_monotonic_ms = 1000u;
  result->operation->idempotency_key = "key-1";
  result->effect_id = "composer-completion-1";
  result->kind = mojom::EffectKind::kModelRequest;
  result->status = mojom::EffectStatus::kCompleted;
  result->model = mojom::ModelEffectResult::New();
  result->model->completion = {'o', 'k'};

  const std::optional<bridge::BridgeComposerCompletion> completion =
      core_service_internal::ToBridgeComposerCompletion(*result);
  ASSERT_TRUE(completion);
  EXPECT_EQ(std::string(completion->effect_id), "composer-completion-1");
  EXPECT_EQ(completion->status,
            static_cast<uint8_t>(mojom::EffectStatus::kCompleted));
  // Unlike a probe's terminal, this one is the answer rather than a verdict
  // about it.
  ASSERT_EQ(completion->completion.size(), 2u);
  EXPECT_EQ(completion->completion[0], static_cast<uint8_t>('o'));
  EXPECT_EQ(completion->completion[1], static_cast<uint8_t>('k'));
}

TEST(RustCoreComposerConversionTest, ANonModelResultIsNotAComposerCompletion) {
  auto result = mojom::EffectResult::New();
  result->operation = mojom::OperationEnvelope::New();
  result->effect_id = "effect-1";
  result->kind = mojom::EffectKind::kNetworkRequest;
  result->status = mojom::EffectStatus::kCompleted;
  EXPECT_FALSE(core_service_internal::ToBridgeComposerCompletion(*result));
}

TEST(RustCoreComposerConversionTest, AClaimedDeliveryProjectsThePush) {
  const mojom::EffectEnvelopePtr projected =
      core_service_internal::ToMojoComposerDelivery(ComposerDelivery());
  ASSERT_TRUE(projected);
  EXPECT_EQ(projected->kind, mojom::EffectKind::kDeliverComposerCompletion);
  EXPECT_EQ(projected->retry_class, mojom::RetryClass::kNever);
  ASSERT_TRUE(projected->composer_completion);
  EXPECT_EQ(projected->composer_completion->request_id, "request-1");
  EXPECT_EQ(projected->composer_completion->text,
            std::optional<std::string>(" two flights."));
}

TEST(RustCoreComposerConversionTest, APushNeedsNoDispatchDeadline) {
  bridge::BridgeComposerDelivery delivery = ComposerDelivery();
  delivery.operation.deadline_monotonic_ms = 0u;
  // The push is not dispatched anywhere, so it is not held to the deadline a
  // dispatched effect needs. Refusing it here would drop a suggestion after
  // the flight had already been settled, which is the one way a composer can
  // be left waiting forever.
  EXPECT_TRUE(core_service_internal::ToMojoComposerDelivery(delivery));

  bridge::BridgeComposerEffect effect = ComposerEffect();
  effect.operation.deadline_monotonic_ms = 0u;
  // The model call is dispatched, and work that leaves the process needs a
  // deadline somebody can enforce.
  EXPECT_FALSE(core_service_internal::ToMojoComposerEffect(effect));
}

TEST(RustCoreComposerConversionTest, ADeliveryWithNoTextProjectsAbsentText) {
  bridge::BridgeComposerDelivery delivery = ComposerDelivery();
  delivery.has_text = false;
  delivery.text = "";
  const mojom::EffectEnvelopePtr projected =
      core_service_internal::ToMojoComposerDelivery(delivery);
  ASSERT_TRUE(projected);
  ASSERT_TRUE(projected->composer_completion);
  // Absent, not empty: a surface handed the empty string would draw nothing
  // and go on waiting for a suggestion that has already arrived.
  EXPECT_FALSE(projected->composer_completion->text.has_value());
}

TEST(RustCoreComposerConversionTest, AnUnclaimedDeliveryProjectsNothing) {
  bridge::BridgeComposerDelivery delivery = ComposerDelivery();
  delivery.claimed = false;
  EXPECT_FALSE(core_service_internal::ToMojoComposerDelivery(delivery));

  bridge::BridgeComposerDelivery contradictory = ComposerDelivery();
  contradictory.has_text = false;
  EXPECT_FALSE(core_service_internal::ToMojoComposerDelivery(contradictory));
}

TEST(RustCoreComposerConversionTest, ASubmissionNamesTheEffectItDisplaced) {
  bridge::BridgeComposerSubmission submission{};
  submission.admission.operation_id = "operation-2";
  submission.admission.status = 0u;  // ACCEPTED
  submission.has_effect = true;
  submission.effect = ComposerEffect();
  submission.has_superseded = true;
  submission.superseded_effect_id = "composer-completion-1";

  CoreResponseBatch batch =
      core_service_internal::ToComposerSubmissionBatch(std::move(submission));
  ASSERT_TRUE(batch.admission);
  EXPECT_EQ(batch.admission->status, mojom::AdmissionStatus::kAccepted);
  EXPECT_EQ(batch.effects.size(), 1u);
  // An effect nobody stops is an effect that still bills, so the displaced
  // dispatch is named rather than forgotten.
  EXPECT_EQ(batch.superseded_effect_id, "composer-completion-1");
  // A suggestion is offered rather than true, so nothing here is published
  // state (decision 0097 section 6).
  EXPECT_TRUE(batch.states.empty());
}

TEST(RustCoreComposerConversionTest, ARefusedSubmissionCarriesNoEffect) {
  bridge::BridgeComposerSubmission submission{};
  submission.admission.operation_id = "operation-2";
  submission.admission.status = 5u;  // INVALID_COMMAND
  submission.has_effect = false;
  submission.has_superseded = false;

  CoreResponseBatch batch =
      core_service_internal::ToComposerSubmissionBatch(std::move(submission));
  ASSERT_TRUE(batch.admission);
  EXPECT_EQ(batch.admission->status, mojom::AdmissionStatus::kInvalidCommand);
  EXPECT_TRUE(batch.effects.empty());
  EXPECT_TRUE(batch.superseded_effect_id.empty());
}

}  // namespace
}  // namespace taffy

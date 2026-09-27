// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/policy_response_validation.h"

#include <string>
#include <utility>

#include "base/containers/span.h"
#include "crypto/sha2.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::AuthoritySubjectPtr Subject() {
  return mojom::AuthoritySubject::New(
      mojom::AuthoritySubjectKind::kDirectUserIntent, "direct-intent-1");
}

mojom::PolicyOriginPtr Origin() {
  auto origin = mojom::PolicyOrigin::New();
  origin->kind = mojom::PolicyOriginKind::kTuple;
  origin->serialization = "https://example.test";
  return origin;
}

mojom::PolicyCapabilityScopePtr Scope() {
  auto scope = mojom::PolicyCapabilityScope::New();
  scope->profile_id = "profile-1";
  scope->tab_id = "tab-1";
  scope->frame_id = "frame-1";
  scope->page_epoch = "epoch-1";
  scope->origin = Origin();
  return scope;
}

mojom::PolicyEvaluationRequestPtr Request() {
  auto request = mojom::PolicyEvaluationRequest::New();
  request->operation = mojom::OperationEnvelope::New("operation-1", 7u, 0u,
                                                     2'500u, "idempotency-1");
  request->now_monotonic_ms = 1'000u;
  request->now_utc_ms = 1'800'000'000'000u;
  request->principal = mojom::PolicyPrincipal::New(
      mojom::PolicyPrincipalKind::kAssistant, std::nullopt);
  request->action_class = mojom::PolicyActionClass::kObservePage;
  request->operation_kind = mojom::TaskActionOperationKind::kDomRead;
  request->proposal_digest = std::string(64u, 'a');
  const auto canonical_intent_digest =
      crypto::SHA256Hash(base::as_byte_span(request->proposal_digest));
  request->canonical_intent_digest.assign(canonical_intent_digest.begin(),
                                          canonical_intent_digest.end());
  request->scope = Scope();
  request->data_classes = {mojom::BipSensitivity::kNotSensitive};
  request->context_risk = mojom::PolicyRiskClass::kLocalRead;
  request->expires_at_monotonic_ms = 2'500u;
  request->actor_lease = mojom::ActorLeaseFact::New();
  request->actor_lease->lease_id = "lease-1";
  request->actor_lease->service_generation = 7u;
  request->actor_lease->profile_id = "profile-1";
  request->actor_lease->tab_id = "tab-1";
  request->actor_lease->control_mode = mojom::TaskControlMode::kUser;
  request->actor_lease->expires_at_monotonic_ms = 3'000u;
  request->actor_lease->authority_subject = Subject();
  request->context = mojom::PolicyEvaluationContext::kDirectUserObservation;
  request->authority_subject = Subject();
  request->policy_version = 1u;
  return request;
}

mojom::PolicyEvaluationRequestPtr TaskNavigateRequest() {
  auto request = Request();
  request->operation->task_revision = 4u;
  request->task_id = "task-1";
  request->action_id = "action-1";
  request->action_class = mojom::PolicyActionClass::kOpenLink;
  request->operation_kind = mojom::TaskActionOperationKind::kNavigate;
  request->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  // The live document is the search results the errand is standing on; the
  // address names a different site, which is the whole point of a navigate.
  request->scope->origin->serialization = "https://www.search.test";
  auto destination = mojom::PolicyOrigin::New();
  destination->kind = mojom::PolicyOriginKind::kTuple;
  destination->serialization = "https://official.test";
  request->scope->destination_scope = std::move(destination);
  request->scope->destination_address = "https://official.test/download";
  request->actor_lease->task_id = request->task_id;
  request->actor_lease->control_mode = mojom::TaskControlMode::kAssistant;
  request->actor_lease->authority_subject = mojom::AuthoritySubject::New(
      mojom::AuthoritySubjectKind::kTask, request->task_id);
  request->context = mojom::PolicyEvaluationContext::kTask;
  request->authority_subject = request->actor_lease->authority_subject.Clone();
  return request;
}

mojom::PolicyEvaluationRequestPtr DiscoveryRequest() {
  auto request = Request();
  request->operation->task_revision = 4u;
  request->task_id = "task-1";
  request->action_id = "action-1";
  request->action_class = mojom::PolicyActionClass::kOpenLink;
  request->operation_kind = mojom::TaskActionOperationKind::kSearch;
  request->scope->origin->kind = mojom::PolicyOriginKind::kOpaque;
  request->scope->origin->serialization.reset();
  request->scope->origin->opaque_id = "opaque-1";
  request->scope->destination_scope = Origin();
  request->scope->destination_address = "https://example.test/search?q=one";
  request->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  request->actor_lease->task_id = request->task_id;
  request->actor_lease->control_mode = mojom::TaskControlMode::kAssistant;
  request->actor_lease->authority_subject = mojom::AuthoritySubject::New(
      mojom::AuthoritySubjectKind::kTask, request->task_id);
  request->context = mojom::PolicyEvaluationContext::kTaskDiscovery;
  request->authority_subject = request->actor_lease->authority_subject.Clone();
  request->discovery = mojom::TaskDiscoveryAuthorityFact::New(
      request->scope->tab_id, "browser-session-1", 2u);
  return request;
}

mojom::MintedCapabilityGrantPtr Grant(
    const mojom::PolicyEvaluationRequest& request) {
  auto grant = mojom::MintedCapabilityGrant::New();
  grant->capability_id = "capability-1";
  grant->service_generation = request.operation->service_generation;
  grant->policy_version = request.policy_version;
  grant->actor_lease_id = request.actor_lease->lease_id;
  grant->task_id = request.task_id;
  grant->action_id = request.action_id;
  grant->action_class = request.action_class;
  grant->operation_kind = request.operation_kind;
  grant->canonical_intent_digest = request.canonical_intent_digest;
  grant->principal = request.principal.Clone();
  grant->proposal_digest = request.proposal_digest;
  grant->idempotency_key = request.operation->idempotency_key;
  grant->scope = request.scope.Clone();
  grant->data_classes = request.data_classes;
  grant->effective_risk = request.context_risk;
  grant->discovery = request.discovery.Clone();
  grant->issued_at_monotonic_ms = request.now_monotonic_ms;
  grant->expires_at_monotonic_ms = request.expires_at_monotonic_ms;
  grant->authority_subject = request.authority_subject.Clone();
  return grant;
}

mojom::EffectEnvelopePtr Effect(const mojom::PolicyEvaluationRequest& request,
                                const mojom::MintedCapabilityGrant& grant) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = request.operation.Clone();
  effect->effect_id = request.operation->operation_id;
  effect->kind = mojom::EffectKind::kPageObservation;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->page_observation = mojom::PageObservationEffect::New();
  auto& body = *effect->page_observation;
  body.tab_id = request.scope->tab_id;
  body.frame_id = request.scope->frame_id;
  body.page_epoch = request.scope->page_epoch;
  body.scope = mojom::ObservationScope::kCurrentDocument;
  body.max_bytes = mojom::kMaxDirectObservationTotalBytes;
  body.capability_id = grant.capability_id;
  body.proposal_digest = request.proposal_digest;
  body.idempotency_key = request.operation->idempotency_key;
  body.authority_subject = request.authority_subject.Clone();
  body.max_nodes = mojom::kMaxDirectObservationNodes;
  body.max_text_bytes = mojom::kMaxDirectObservationTextBytes;
  body.max_frames = mojom::kMaxDirectObservationFrames;
  body.deadline_ms = mojom::kMaxDirectObservationDeadlineMs;
  return effect;
}

// Decision 0136 section 3: a model may name a site and the browser goes there.
// This clause used to require the destination to be the document's own origin,
// so every cross-origin navigate was refused before the core was asked — the
// errand searched, read the results, was refused the site they named, and
// searched again.
TEST(PolicyResponseValidationTest, TaskNavigateReachesTheSiteTheAddressNames) {
  EXPECT_FALSE(TaskScopeRefusalForOperation(*TaskNavigateRequest()));
}

TEST(PolicyResponseValidationTest, TaskNavigateOriginOnlyAddressIsAdmitted) {
  auto request = TaskNavigateRequest();
  request->scope->destination_address = "https://official.test/";
  EXPECT_FALSE(TaskScopeRefusalForOperation(*request));
}

TEST(PolicyResponseValidationTest, TaskNavigateRefusesADestinationItDoesNotName) {
  auto request = TaskNavigateRequest();
  request->scope->destination_address = "https://elsewhere.test/download";
  EXPECT_EQ(TaskScopeRefusalForOperation(*request),
            mojom::TaskActionResultCode::kEgressNotAuthorized);
}

TEST(PolicyResponseValidationTest, TaskNavigateRefusesANonHttpsDestination) {
  auto request = TaskNavigateRequest();
  request->scope->destination_scope->serialization = "http://official.test";
  request->scope->destination_address = "http://official.test/download";
  EXPECT_EQ(TaskScopeRefusalForOperation(*request),
            mojom::TaskActionResultCode::kEgressNotAuthorized);
}

TEST(PolicyResponseValidationTest, TaskNavigateNamesNoNode) {
  auto request = TaskNavigateRequest();
  request->scope->node_id = "node-1";
  EXPECT_EQ(TaskScopeRefusalForOperation(*request),
            mojom::TaskActionResultCode::kUnsupported);
}

// Decision 0176: a task standing on a document with no site of its own may
// still leave it. An address that does not resolve commits Chromium's own
// error document, whose origin is opaque; refusing the scope outright refused
// the only move that recovers, and the errand died on the error page.
TEST(PolicyResponseValidationTest, TaskNavigateLeavesADocumentWithNoSite) {
  auto request = TaskNavigateRequest();
  request->scope->origin->kind = mojom::PolicyOriginKind::kOpaque;
  request->scope->origin->serialization.reset();
  request->scope->origin->opaque_id = "epoch-of-the-error-document";
  EXPECT_FALSE(TaskScopeRefusalForOperation(*request));

  // A search from the same document is the other admitted move.
  auto searching = request.Clone();
  searching->operation_kind = mojom::TaskActionOperationKind::kSearch;
  searching->scope->destination_address = "https://official.test/?q=aadhaar";
  EXPECT_FALSE(TaskScopeRefusalForOperation(*searching));

  // Where it goes is still decided the same way.
  auto elsewhere = request.Clone();
  elsewhere->scope->destination_address = "https://elsewhere.test/download";
  EXPECT_EQ(TaskScopeRefusalForOperation(*elsewhere),
            mojom::TaskActionResultCode::kEgressNotAuthorized);
}

// Everything else is refused from such a document, because nothing downstream
// can bind it: a read has no source, and a node-targeted action has no graph.
TEST(PolicyResponseValidationTest, ADocumentWithNoSiteAdmitsOnlyLeaving) {
  auto reading = TaskNavigateRequest();
  reading->scope->origin->kind = mojom::PolicyOriginKind::kOpaque;
  reading->scope->origin->serialization.reset();
  reading->scope->origin->opaque_id = "epoch-of-the-error-document";
  reading->operation_kind = mojom::TaskActionOperationKind::kDomRead;
  reading->action_class = mojom::PolicyActionClass::kObservePage;
  reading->scope->destination_scope.reset();
  reading->scope->destination_address.reset();
  EXPECT_EQ(TaskScopeRefusalForOperation(*reading),
            mojom::TaskActionResultCode::kUnsupported);

  auto opening_a_tab = TaskNavigateRequest();
  opening_a_tab->scope->origin->kind = mojom::PolicyOriginKind::kOpaque;
  opening_a_tab->scope->origin->serialization.reset();
  opening_a_tab->scope->origin->opaque_id = "epoch-of-the-error-document";
  opening_a_tab->operation_kind = mojom::TaskActionOperationKind::kTabsOpen;
  opening_a_tab->action_class = mojom::PolicyActionClass::kCreateTaskTab;
  EXPECT_EQ(TaskScopeRefusalForOperation(*opening_a_tab),
            mojom::TaskActionResultCode::kUnsupported);

  // An opaque identity that is not one, and a graph revision a document with
  // no graph cannot have, are both refused as shape.
  auto unnamed = TaskNavigateRequest();
  unnamed->scope->origin->kind = mojom::PolicyOriginKind::kOpaque;
  unnamed->scope->origin->serialization.reset();
  unnamed->scope->origin->opaque_id = "";
  EXPECT_EQ(TaskScopeRefusalForOperation(*unnamed),
            mojom::TaskActionResultCode::kUnsupported);

  auto with_a_graph = TaskNavigateRequest();
  with_a_graph->scope->origin->kind = mojom::PolicyOriginKind::kOpaque;
  with_a_graph->scope->origin->serialization.reset();
  with_a_graph->scope->origin->opaque_id = "epoch-of-the-error-document";
  with_a_graph->scope->required_graph_revision = 3u;
  EXPECT_EQ(TaskScopeRefusalForOperation(*with_a_graph),
            mojom::TaskActionResultCode::kUnsupported);
}

// An observation is not a destination-bearing operation, so its refusal is a
// shape refusal and never reads as egress.
TEST(PolicyResponseValidationTest, TaskObservationRefusalNamesTheShape) {
  auto request = TaskNavigateRequest();
  request->operation_kind = mojom::TaskActionOperationKind::kDomRead;
  request->action_class = mojom::PolicyActionClass::kObservePage;
  EXPECT_EQ(TaskScopeRefusalForOperation(*request),
            mojom::TaskActionResultCode::kUnsupported);
}

TEST(PolicyResponseValidationTest, ExactDirectGrantAndEffectMatch) {
  auto request = Request();
  auto grant = Grant(*request);
  auto effect = Effect(*request, *grant);

  EXPECT_TRUE(PolicyGrantMatchesRequest(*grant, *request));
  EXPECT_TRUE(DirectObservationEffectMatchesRequest(*effect, *grant, *request));
}

TEST(PolicyResponseValidationTest, ForgedGrantBindingsFailClosed) {
  auto request = Request();
  auto grant = Grant(*request);
  grant->proposal_digest = std::string(64u, 'b');
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));

  grant = Grant(*request);
  grant->scope->origin->serialization = "https://forged.test";
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));

  grant = Grant(*request);
  ++grant->expires_at_monotonic_ms;
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));

  grant = Grant(*request);
  grant->authority_subject->authority_subject_id = "direct-intent-forged";
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));

  grant = Grant(*request);
  ++grant->policy_version;
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));

  grant = Grant(*request);
  grant->operation_kind = mojom::TaskActionOperationKind::kDomQuery;
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));

  grant = Grant(*request);
  grant->canonical_intent_digest[0] ^= 0xffu;
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));

  grant = Grant(*request);
  grant->scope->destination_address = "https://example.test/forged";
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));
}

TEST(PolicyResponseValidationTest, DiscoveryGrantMustEchoExactAuthority) {
  auto request = DiscoveryRequest();
  auto grant = Grant(*request);
  EXPECT_TRUE(PolicyGrantMatchesRequest(*grant, *request));

  grant->discovery->discovery_tab_id = "tab-forged";
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));

  grant = Grant(*request);
  grant->discovery->browser_session_id = "browser-session-forged";
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));

  grant = Grant(*request);
  ++grant->discovery->remaining_new_source_cap;
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));

  grant = Grant(*request);
  grant->discovery.reset();
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));

  auto ordinary_request = Request();
  auto ordinary_grant = Grant(*ordinary_request);
  ordinary_grant->discovery =
      mojom::TaskDiscoveryAuthorityFact::New("tab-1", "browser-session-1", 1u);
  EXPECT_FALSE(PolicyGrantMatchesRequest(*ordinary_grant, *ordinary_request));
}

TEST(PolicyResponseValidationTest,
     DiscoveryRequestRequiresExactOpaqueToTupleShape) {
  auto request = DiscoveryRequest();
  EXPECT_TRUE(
      TaskDiscoveryPolicyRequestHasExactShape(*request, "browser-session-1"));
  EXPECT_FALSE(
      TaskDiscoveryPolicyRequestHasExactShape(*request, "stale-session"));

  request = DiscoveryRequest();
  request->discovery->remaining_new_source_cap = 0u;
  EXPECT_FALSE(
      TaskDiscoveryPolicyRequestHasExactShape(*request, "browser-session-1"));

  request = DiscoveryRequest();
  request->scope->origin = Origin();
  EXPECT_FALSE(
      TaskDiscoveryPolicyRequestHasExactShape(*request, "browser-session-1"));

  request = DiscoveryRequest();
  request->scope->destination_scope->kind = mojom::PolicyOriginKind::kOpaque;
  request->scope->destination_scope->serialization.reset();
  request->scope->destination_scope->opaque_id = "opaque-destination";
  EXPECT_FALSE(
      TaskDiscoveryPolicyRequestHasExactShape(*request, "browser-session-1"));

  request = DiscoveryRequest();
  request->context = mojom::PolicyEvaluationContext::kTask;
  EXPECT_FALSE(
      TaskDiscoveryPolicyRequestHasExactShape(*request, "browser-session-1"));
}

TEST(PolicyResponseValidationTest,
     KnownHttpsDiscoveryKeepsExactAuthorityShape) {
  auto request = DiscoveryRequest();
  request->operation_kind = mojom::TaskActionOperationKind::kNavigate;
  request->scope->destination_address =
      "https://example.test/download-document";
  ASSERT_TRUE(
      TaskDiscoveryPolicyRequestHasExactShape(*request, "browser-session-1"));
  auto grant = Grant(*request);
  EXPECT_TRUE(PolicyGrantMatchesRequest(*grant, *request));
  grant->scope->destination_address = "https://example.test/different";
  EXPECT_FALSE(PolicyGrantMatchesRequest(*grant, *request));
  request->operation_kind = mojom::TaskActionOperationKind::kTabsOpen;
  EXPECT_FALSE(
      TaskDiscoveryPolicyRequestHasExactShape(*request, "browser-session-1"));
  request->operation_kind = mojom::TaskActionOperationKind::kNavigate;
  request->scope->destination_address = "http://example.test/download-document";
  request->scope->destination_scope->serialization = "http://example.test";
  EXPECT_FALSE(
      TaskDiscoveryPolicyRequestHasExactShape(*request, "browser-session-1"));
  request->scope->destination_address =
      "https://example.test/download-document";
  request->scope->destination_scope->serialization = "https://example.test";
  EXPECT_FALSE(
      TaskDiscoveryPolicyRequestHasExactShape(*request, "previous-session"));
  request->scope->node_id = "node-1";
  EXPECT_FALSE(
      TaskDiscoveryPolicyRequestHasExactShape(*request, "browser-session-1"));
}

TEST(PolicyResponseValidationTest, ForgedDirectEffectOperandsFailClosed) {
  auto request = Request();
  auto grant = Grant(*request);
  auto effect = Effect(*request, *grant);
  effect->page_observation->frame_id = "frame-forged";
  EXPECT_FALSE(
      DirectObservationEffectMatchesRequest(*effect, *grant, *request));

  effect = Effect(*request, *grant);
  effect->page_observation->scope = mojom::ObservationScope::kSelectedSources;
  EXPECT_FALSE(
      DirectObservationEffectMatchesRequest(*effect, *grant, *request));

  effect = Effect(*request, *grant);
  ++effect->page_observation->deadline_ms;
  EXPECT_FALSE(
      DirectObservationEffectMatchesRequest(*effect, *grant, *request));

  effect = Effect(*request, *grant);
  effect->page_observation->task_id = "task-forged";
  EXPECT_FALSE(
      DirectObservationEffectMatchesRequest(*effect, *grant, *request));

  effect = Effect(*request, *grant);
  effect->page_observation->expected_graph_revision = 1u;
  EXPECT_FALSE(
      DirectObservationEffectMatchesRequest(*effect, *grant, *request));
}

}  // namespace
}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <array>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/services/core/rust_core_account.h"
#include "taffy/services/core/rust_core_command_conversions.h"
#include "taffy/services/core/rust_core_policy.h"
#include "taffy/services/core/rust_core_response.h"
#include "testing/gtest/include/gtest/gtest.h"

// What these cover: the boundary where a `uint8_t` produced by the sandboxed
// Rust core becomes a member of a closed Mojo enumeration the browser's
// authority ledger trusts. Every enumeration in the Core Service contract is
// closed and an unknown value must fail closed, but C++ is the one language
// where the unchecked conversion compiles: `static_cast<Enum>(byte)` for a
// value that names no enumerator is undefined behaviour, and here it is also
// a fail-open, because the grant or effect leaves the seam carrying a value
// no later switch can match.
//
// Each refusal below is reached with a byte one past the contract's last
// member, so a member appended to the schema does not quietly turn a negative
// test into a positive one: the generated decoder gains the case, and the test
// fails until the value is moved.

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;
namespace wire = core_service::wire;

bridge::BridgeAuthoritySubject TaskSubject() {
  bridge::BridgeAuthoritySubject subject{};
  subject.kind = 0u;  // TASK
  subject.authority_subject_id = "authority-subject-1";
  return subject;
}

bridge::BridgeMintedGrant ValidGrant() {
  bridge::BridgeMintedGrant grant{};
  grant.capability_id = "capability-1";
  grant.service_generation = 1u;
  grant.policy_version = 1u;
  grant.actor_lease_id = "lease-1";
  grant.task_id = "task-1";
  grant.action_id = "action-1";
  grant.action_class = 0u;    // OBSERVE_PAGE
  grant.operation_kind = 9u;  // DOM_READ
  grant.canonical_intent_digest[0] = 0xa5u;
  grant.principal.kind = 0u;  // ASSISTANT
  grant.proposal_digest = "digest-1";
  grant.idempotency_key = "key-1";
  grant.scope.profile_id = "profile-1";
  grant.scope.tab_id = "tab-1";
  grant.scope.frame_id = "frame-1";
  grant.scope.page_epoch = "epoch-1";
  grant.scope.origin.kind = 0u;  // TUPLE
  grant.scope.origin.has_serialization = true;
  grant.scope.origin.serialization = "https://example.test";
  grant.data_classes = {0u};  // NOT_SENSITIVE
  grant.effective_risk = 0u;  // LOCAL_READ
  grant.authority_subject = TaskSubject();
  return grant;
}

bridge::BridgePolicyResult GrantedResult() {
  bridge::BridgePolicyResult result{};
  result.operation_id = "operation-1";
  result.status = 0u;  // GRANTED
  result.has_minted_grant = true;
  result.minted_grant = ValidGrant();
  return result;
}

mojom::PolicyEvaluationRequestPtr DiscoveryPolicyRequest() {
  auto request = mojom::PolicyEvaluationRequest::New();
  request->operation = mojom::OperationEnvelope::New(
      "operation-discovery", 7u, 3u, 9'000u, "discovery-key");
  request->now_monotonic_ms = 1'000u;
  request->now_utc_ms = 2'000u;
  request->action_id = "action-discovery";
  request->task_id = "task-discovery";
  request->principal = mojom::PolicyPrincipal::New();
  request->principal->kind = mojom::PolicyPrincipalKind::kAssistant;
  request->action_class = mojom::PolicyActionClass::kOpenLink;
  request->operation_kind = mojom::TaskActionOperationKind::kSearch;
  request->canonical_intent_digest = std::vector<uint8_t>(32u, 0x5au);
  request->proposal_digest = "proposal-discovery";
  request->scope = mojom::PolicyCapabilityScope::New();
  request->scope->profile_id = "profile-1";
  request->scope->tab_id = "discovery-tab-1";
  request->scope->frame_id = "frame-1";
  request->scope->page_epoch = "opaque-document-1";
  request->scope->origin = mojom::PolicyOrigin::New();
  request->scope->origin->kind = mojom::PolicyOriginKind::kOpaque;
  request->scope->origin->opaque_id = "opaque-document-1";
  request->scope->destination_scope = mojom::PolicyOrigin::New();
  request->scope->destination_scope->kind = mojom::PolicyOriginKind::kTuple;
  request->scope->destination_scope->serialization = "https://search.example";
  request->scope->destination_address = "https://search.example/?q=taffy";
  request->data_classes = {mojom::BipSensitivity::kNotSensitive};
  request->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  request->expires_at_monotonic_ms = 8'000u;
  request->actor_lease = mojom::ActorLeaseFact::New();
  request->actor_lease->lease_id = "lease-discovery";
  request->actor_lease->service_generation = 7u;
  request->actor_lease->task_id = "task-discovery";
  request->actor_lease->profile_id = "profile-1";
  request->actor_lease->tab_id = "discovery-tab-1";
  request->actor_lease->control_mode = mojom::TaskControlMode::kAssistant;
  request->actor_lease->expires_at_monotonic_ms = 8'000u;
  request->actor_lease->authority_subject = mojom::AuthoritySubject::New(
      mojom::AuthoritySubjectKind::kTask, "task-discovery");
  request->context = mojom::PolicyEvaluationContext::kTaskDiscovery;
  request->authority_subject = mojom::AuthoritySubject::New(
      mojom::AuthoritySubjectKind::kTask, "task-discovery");
  request->policy_version = 4u;
  request->discovery = mojom::TaskDiscoveryAuthorityFact::New(
      "discovery-tab-1", "browser-session-1", 4u);
  return request;
}

bridge::BridgeAccountEffect OAuthSurfaceEffect() {
  bridge::BridgeAccountEffect effect{};
  effect.operation.operation_id = "operation-1";
  effect.operation.service_generation = 1u;
  effect.effect_id = "effect-1";
  effect.kind = 7u;            // OPEN_AUTH_SURFACE
  effect.retry_class = 0u;     // IDEMPOTENT
  effect.operation_kind = 0u;  // OPEN_OAUTH
  effect.flow_id = "flow-1";
  effect.auth_method = 0u;   // GOOGLE
  effect.scopes = {0u, 1u};  // OPEN_ID, EMAIL
  effect.redirect_binding_id = "primary-auth-callback";
  effect.pkce_challenge = "challenge";
  effect.pkce_verifier_handle = "verifier-handle";
  effect.state = "state";
  return effect;
}

bridge::BridgeStorageEffect SkillRunEffect() {
  bridge::BridgeStorageEffect effect{};
  effect.operation.operation_id = "skill-run-operation-1";
  effect.operation.service_generation = 1u;
  effect.operation.task_revision = 9u;
  effect.operation.deadline_monotonic_ms = 10'000u;
  effect.operation.idempotency_key = "skill-run-key-1";
  effect.effect_id = "skill-run-effect-1";
  effect.operation_kind = 6u;  // RECORD_SKILL_RUN
  effect.skill_id = "source-table";
  effect.skill_version = 1u;
  effect.skill_task_id = "task-1";
  effect.skill_run_outcome = 0u;  // COMPLETED
  effect.skill_ran_at_utc_ms = 2'000u;
  return effect;
}

bridge::BridgeAccountTokenValidationResult ValidatedToken() {
  bridge::BridgeAccountTokenValidationResult token{};
  token.status = 0u;  // VALIDATED
  token.operation_id = "operation-1";
  token.operation_kind = 0u;  // EXCHANGE_AUTHORIZATION_CODE
  token.auth_method = 0u;     // GOOGLE
  token.account_subject = "subject-1";
  token.expires_in_seconds = 3600u;
  return token;
}

// --- the generated decoder itself -------------------------------------------

TEST(CoreServiceEnumDecoderTest, KnownWireValueDecodes) {
  EXPECT_EQ(mojom::AccountAuthMethod::kGoogle,
            wire::AccountAuthMethodFromWire(0u));
  EXPECT_EQ(mojom::AccountAuthMethod::kFacebook,
            wire::AccountAuthMethodFromWire(3u));
  EXPECT_EQ(mojom::PolicyEvaluationStatus::kCoreUnavailable,
            wire::PolicyEvaluationStatusFromWire(4u));
  EXPECT_EQ(mojom::PolicyActionClass::kExecuteToolJob,
            wire::PolicyActionClassFromWire(16u));
  EXPECT_EQ(mojom::TaskActionOperationKind::kDomRead,
            wire::TaskActionOperationKindFromWire(9u));
}

TEST(CoreServiceEnumDecoderTest, UnknownWireValueIsRefused) {
  EXPECT_EQ(std::nullopt, wire::AccountAuthMethodFromWire(4u));
  EXPECT_EQ(std::nullopt, wire::AccountAuthMethodFromWire(255u));
  EXPECT_EQ(std::nullopt, wire::PolicyEvaluationStatusFromWire(5u));
  EXPECT_EQ(std::nullopt, wire::PolicyActionClassFromWire(255u));
  EXPECT_EQ(std::nullopt, wire::TaskActionOperationKindFromWire(255u));
  EXPECT_EQ(std::nullopt, wire::BipSensitivityFromWire(255u));
}

// --- storage ---------------------------------------------------------------

TEST(RustCoreStorageConversionTest, SkillRunProjectsWithoutTaskCommitFields) {
  mojom::EffectEnvelopePtr projected =
      core_service_internal::ToMojoStorageEffect(SkillRunEffect());

  ASSERT_TRUE(projected);
  ASSERT_TRUE(projected->storage_commit);
  EXPECT_EQ(mojom::StorageOperation::kRecordSkillRun,
            projected->storage_commit->operation_kind);
  ASSERT_TRUE(projected->storage_commit->skill_run);
  EXPECT_EQ("source-table", projected->storage_commit->skill_run->skill_id);
  EXPECT_EQ(1u, projected->storage_commit->skill_run->version);
  EXPECT_EQ("task-1", projected->storage_commit->skill_run->task_id);
  EXPECT_EQ(mojom::SkillRunOutcome::kCompleted,
            projected->storage_commit->skill_run->outcome);
  EXPECT_TRUE(projected->storage_commit->task_id.empty());
  EXPECT_TRUE(projected->storage_commit->transaction_batch.empty());
  EXPECT_EQ(32u, projected->storage_commit->task_id_seed.size());
}

TEST(RustCoreStorageConversionTest, UnknownStorageOperationIsRefused) {
  bridge::BridgeStorageEffect effect = SkillRunEffect();
  effect.operation_kind = 9u;

  EXPECT_FALSE(core_service_internal::ToMojoStorageEffect(effect));
}

TEST(RustCoreStorageConversionTest, KnownUnservedStorageOperationIsRefused) {
  bridge::BridgeStorageEffect effect = SkillRunEffect();
  effect.operation_kind = 1u;  // QUERY_WORKSPACE

  EXPECT_FALSE(core_service_internal::ToMojoStorageEffect(effect));
}

TEST(RustCoreStorageConversionTest, UnknownSkillRunOutcomeIsRefused) {
  bridge::BridgeStorageEffect effect = SkillRunEffect();
  effect.skill_run_outcome = 4u;

  EXPECT_FALSE(core_service_internal::ToMojoStorageEffect(effect));
}

// --- policy -----------------------------------------------------------------

TEST(RustCorePolicyConversionTest, ValidResultProjects) {
  mojom::PolicyEvaluationResultPtr projected =
      core_service_internal::ToMojoPolicyResult(GrantedResult());

  ASSERT_TRUE(projected);
  EXPECT_EQ(mojom::PolicyEvaluationStatus::kGranted, projected->status);
  ASSERT_TRUE(projected->minted_grant);
  EXPECT_EQ(mojom::PolicyActionClass::kObservePage,
            projected->minted_grant->action_class);
  EXPECT_EQ(mojom::TaskActionOperationKind::kDomRead,
            projected->minted_grant->operation_kind);
  EXPECT_EQ(0xa5u, projected->minted_grant->canonical_intent_digest[0]);
  EXPECT_EQ(mojom::PolicyRiskClass::kLocalRead,
            projected->minted_grant->effective_risk);
}

TEST(RustCorePolicyConversionTest,
     DiscoveryAuthorityCrossesRequestAndGrantWithoutReconstruction) {
  mojom::PolicyEvaluationRequestPtr request = DiscoveryPolicyRequest();
  const std::optional<bridge::BridgePolicyRequest> bridged =
      core_service_internal::ToBridgePolicyRequest(*request);

  ASSERT_TRUE(bridged);
  std::array<uint8_t, 32u> expected_intent_digest;
  expected_intent_digest.fill(0x5au);
  EXPECT_EQ(expected_intent_digest, bridged->canonical_intent_digest);
  EXPECT_TRUE(bridged->has_discovery);
  EXPECT_EQ("discovery-tab-1",
            std::string(bridged->discovery.discovery_tab_id));
  EXPECT_EQ("browser-session-1",
            std::string(bridged->discovery.browser_session_id));
  EXPECT_EQ(4u, bridged->discovery.remaining_new_source_cap);

  bridge::BridgePolicyResult result = GrantedResult();
  result.minted_grant.has_discovery = true;
  result.minted_grant.discovery.discovery_tab_id = "discovery-tab-1";
  result.minted_grant.discovery.browser_session_id = "browser-session-1";
  result.minted_grant.discovery.remaining_new_source_cap = 4u;
  result.minted_grant.scope.origin.kind = 1u;  // OPAQUE
  result.minted_grant.scope.origin.has_serialization = false;
  result.minted_grant.scope.origin.serialization = "";
  result.minted_grant.scope.origin.has_opaque_id = true;
  result.minted_grant.scope.origin.opaque_id = "opaque-document-1";

  mojom::PolicyEvaluationResultPtr projected =
      core_service_internal::ToMojoPolicyResult(std::move(result));

  ASSERT_TRUE(projected);
  ASSERT_TRUE(projected->minted_grant);
  ASSERT_TRUE(projected->minted_grant->discovery);
  EXPECT_EQ("discovery-tab-1",
            projected->minted_grant->discovery->discovery_tab_id);
  EXPECT_EQ("browser-session-1",
            projected->minted_grant->discovery->browser_session_id);
  EXPECT_EQ(4u, projected->minted_grant->discovery->remaining_new_source_cap);
}

TEST(RustCorePolicyConversionTest, UnknownEvaluationStatusIsRefused) {
  bridge::BridgePolicyResult result = GrantedResult();
  result.status = 5u;

  EXPECT_FALSE(core_service_internal::ToMojoPolicyResult(std::move(result)));
}

TEST(RustCorePolicyConversionTest, UnknownActionClassIsRefused) {
  bridge::BridgePolicyResult result = GrantedResult();
  result.minted_grant.action_class = 255u;

  EXPECT_FALSE(core_service_internal::ToMojoPolicyResult(std::move(result)));
}

TEST(RustCorePolicyConversionTest, UnknownActionOperationIsRefused) {
  bridge::BridgePolicyResult result = GrantedResult();
  result.minted_grant.operation_kind = 255u;

  EXPECT_FALSE(core_service_internal::ToMojoPolicyResult(std::move(result)));
}

TEST(RustCorePolicyConversionTest, UnknownRiskClassIsRefused) {
  bridge::BridgePolicyResult result = GrantedResult();
  result.minted_grant.effective_risk = 5u;

  EXPECT_FALSE(core_service_internal::ToMojoPolicyResult(std::move(result)));
}

TEST(RustCorePolicyConversionTest, UnknownDataClassIsRefused) {
  bridge::BridgePolicyResult result = GrantedResult();
  result.minted_grant.data_classes = {255u};

  EXPECT_FALSE(core_service_internal::ToMojoPolicyResult(std::move(result)));
}

TEST(RustCorePolicyConversionTest, UnknownAuthoritySubjectKindIsRefused) {
  bridge::BridgePolicyResult result = GrantedResult();
  result.minted_grant.authority_subject.kind = 2u;

  EXPECT_FALSE(core_service_internal::ToMojoPolicyResult(std::move(result)));
}

TEST(RustCorePolicyConversionTest, UnknownPrincipalKindIsRefused) {
  bridge::BridgePolicyResult result = GrantedResult();
  result.minted_grant.principal.kind = 2u;

  EXPECT_FALSE(core_service_internal::ToMojoPolicyResult(std::move(result)));
}

TEST(RustCorePolicyConversionTest, UnknownOriginKindIsRefused) {
  bridge::BridgePolicyResult result = GrantedResult();
  result.minted_grant.scope.origin.kind = 2u;

  EXPECT_FALSE(core_service_internal::ToMojoPolicyResult(std::move(result)));
}

TEST(RustCorePolicyConversionTest, UnknownRedirectOriginKindIsRefused) {
  bridge::BridgePolicyResult result = GrantedResult();
  bridge::BridgeOrigin redirect{};
  redirect.kind = 2u;
  result.minted_grant.scope.allowed_redirects.push_back(std::move(redirect));

  EXPECT_FALSE(core_service_internal::ToMojoPolicyResult(std::move(result)));
}

TEST(RustCorePolicyConversionTest, UnknownObservationScopeIsRefused) {
  bridge::BridgePolicyResult result = GrantedResult();
  result.has_direct_observation_effect = true;
  result.direct_observation_effect.effect_id = "effect-1";
  result.direct_observation_effect.authority_subject = TaskSubject();
  result.direct_observation_effect.scope = 2u;

  EXPECT_FALSE(core_service_internal::ToMojoPolicyResult(std::move(result)));
}

// --- account ----------------------------------------------------------------

TEST(RustCoreAccountConversionTest, ValidAuthSurfaceEffectProjects) {
  mojom::EffectEnvelopePtr projected =
      core_service_internal::ToMojoAccountEffect(OAuthSurfaceEffect());

  ASSERT_TRUE(projected);
  EXPECT_EQ(mojom::EffectKind::kOpenAuthSurface, projected->kind);
  ASSERT_TRUE(projected->auth_surface);
  ASSERT_TRUE(projected->auth_surface->oauth);
  EXPECT_EQ(mojom::AccountAuthMethod::kGoogle,
            projected->auth_surface->oauth->auth_method);
  EXPECT_EQ(2u, projected->auth_surface->oauth->scopes.size());
}

TEST(RustCoreAccountConversionTest, UnknownEffectKindIsRefused) {
  bridge::BridgeAccountEffect effect = OAuthSurfaceEffect();
  effect.kind = 9u;

  EXPECT_FALSE(core_service_internal::ToMojoAccountEffect(effect));
}

TEST(RustCoreAccountConversionTest, UnknownRetryClassIsRefused) {
  bridge::BridgeAccountEffect effect = OAuthSurfaceEffect();
  effect.retry_class = 3u;

  EXPECT_FALSE(core_service_internal::ToMojoAccountEffect(effect));
}

TEST(RustCoreAccountConversionTest, UnknownAuthMethodIsRefused) {
  bridge::BridgeAccountEffect effect = OAuthSurfaceEffect();
  effect.auth_method = 4u;

  EXPECT_FALSE(core_service_internal::ToMojoAccountEffect(effect));
}

TEST(RustCoreAccountConversionTest, UnknownAccountScopeIsRefused) {
  bridge::BridgeAccountEffect effect = OAuthSurfaceEffect();
  effect.scopes = {3u};

  EXPECT_FALSE(core_service_internal::ToMojoAccountEffect(effect));
}

TEST(RustCoreAccountConversionTest, UnknownAuthSurfaceOperationIsRefused) {
  bridge::BridgeAccountEffect effect = OAuthSurfaceEffect();
  effect.operation_kind = 2u;

  EXPECT_FALSE(core_service_internal::ToMojoAccountEffect(effect));
}

TEST(RustCoreAccountConversionTest, UnknownSecureStoreOperationIsRefused) {
  bridge::BridgeAccountEffect effect = OAuthSurfaceEffect();
  effect.kind = 6u;  // SECURE_STORE
  effect.operation_kind = 3u;

  EXPECT_FALSE(core_service_internal::ToMojoAccountEffect(effect));
}

TEST(RustCoreAccountConversionTest, UnknownSecretPurposeIsRefused) {
  bridge::BridgeAccountEffect effect = OAuthSurfaceEffect();
  effect.kind = 6u;            // SECURE_STORE
  effect.operation_kind = 1u;  // WRITE_TRANSIENT
  effect.purpose = 2u;

  EXPECT_FALSE(core_service_internal::ToMojoAccountEffect(effect));
}

TEST(RustCoreAccountConversionTest, UnknownNetworkOperationIsRefused) {
  bridge::BridgeAccountEffect effect = OAuthSurfaceEffect();
  effect.kind = 3u;  // NETWORK_REQUEST
  effect.operation_kind = 5u;

  EXPECT_FALSE(core_service_internal::ToMojoAccountEffect(effect));
}

TEST(RustCoreAccountConversionTest, ValidTokenValidationResultProjects) {
  mojom::AccountTokenValidationResultPtr projected =
      core_service_internal::ToMojoAccountTokenValidationResult(
          ValidatedToken());

  ASSERT_TRUE(projected);
  EXPECT_EQ(mojom::AccountTokenValidationStatus::kValidated, projected->status);
  EXPECT_EQ(mojom::AccountAuthMethod::kGoogle, projected->auth_method);
}

TEST(RustCoreAccountConversionTest, UnknownTokenValidationStatusIsRefused) {
  bridge::BridgeAccountTokenValidationResult token = ValidatedToken();
  token.status = 5u;

  EXPECT_FALSE(core_service_internal::ToMojoAccountTokenValidationResult(
      std::move(token)));
}

TEST(RustCoreAccountConversionTest, UnknownTokenAuthMethodIsRefused) {
  bridge::BridgeAccountTokenValidationResult token = ValidatedToken();
  token.auth_method = 4u;

  EXPECT_FALSE(core_service_internal::ToMojoAccountTokenValidationResult(
      std::move(token)));
}

TEST(RustCoreAccountConversionTest, UnknownTokenNetworkOperationIsRefused) {
  bridge::BridgeAccountTokenValidationResult token = ValidatedToken();
  token.operation_kind = 255u;

  EXPECT_FALSE(core_service_internal::ToMojoAccountTokenValidationResult(
      std::move(token)));
}

}  // namespace
}  // namespace taffy

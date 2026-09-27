// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/bip_request_builder.h"

#include <utility>

#include "base/check.h"
#include "base/time/time.h"
#include "taffy/components/intelligence/content/action_digest.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/test/support/deterministic_clock.h"

namespace taffy::test {

namespace {

// Long enough that a slow builder cannot expire a capability mid-test, short
// enough that a test asserting expiry can reach it by advancing a deterministic
// clock rather than by sleeping.
constexpr base::TimeDelta kCapabilityLifetime = base::Seconds(30);
constexpr base::TimeDelta kActionDeadline = base::Seconds(20);

// The read-oriented adapter set. Deliberately not every adapter: a request that
// asked for everything would make an adapter-unavailable result look like the
// property under test failing.
//
// kForms is in it, and it was not until 2026-08-20. The renderer runs only the
// adapters a request names - snapshot_builder.cc skips any other producing
// adapter outright - and the form schema adapter is the only thing that
// classifies a credential control and emits the ValueKind::kSecretWithheld
// placeholder the redaction summary counts. Leaving it out meant every
// observation in this suite reported
// `redaction.suppressed_secret_value_count == 0`, including the ones taken of
// the seeded-secret checkout fixture, and the four SensitiveZoneTest cases
// that assert suppression was reported failed against a renderer that was
// behaving correctly. A suite that asks for less than it asserts proves less
// than it claims - the same reason renderer/test/endpoint_test_harness.cc
// gives for its own adapter list.
//
// It is kOptional, not kRequired: a fixture with no form controls reports the
// forms adapter unsupported, and a required adapter reporting unsupported
// refuses the whole observation. Optional keeps that page observable and still
// runs the adapter wherever there is a control to classify.
std::vector<AdapterRequirement> ReadOrientedAdapters() {
  return {
      AdapterRequirement{AdapterKind::kDom, AdapterRequirementLevel::kRequired},
      AdapterRequirement{AdapterKind::kAccessibility,
                         AdapterRequirementLevel::kOptional},
      AdapterRequirement{AdapterKind::kForms,
                         AdapterRequirementLevel::kOptional},
      AdapterRequirement{AdapterKind::kMetadata,
                         AdapterRequirementLevel::kOptional},
  };
}

std::vector<SemanticField> ReadOrientedFields() {
  return {SemanticField::kRole,        SemanticField::kName,
          SemanticField::kTextRuns,    SemanticField::kStates,
          SemanticField::kDestination, SemanticField::kActions,
          SemanticField::kSensitivity, SemanticField::kEdges,
          SemanticField::kFrames};
}

std::vector<AdapterRequirement> FormObservationAdapters() {
  return {
      AdapterRequirement{AdapterKind::kDom, AdapterRequirementLevel::kOptional},
      AdapterRequirement{AdapterKind::kForms,
                         AdapterRequirementLevel::kRequired},
      AdapterRequirement{AdapterKind::kLayout,
                         AdapterRequirementLevel::kOptional},
  };
}

std::vector<SemanticField> FormObservationFields() {
  return {
      SemanticField::kRole,        SemanticField::kName,
      SemanticField::kDescription, SemanticField::kTextRuns,
      SemanticField::kStates,      SemanticField::kValueDescriptor,
      SemanticField::kDestination, SemanticField::kBounds,
      SemanticField::kActions,     SemanticField::kAttributes,
      SemanticField::kSensitivity, SemanticField::kEdges,
  };
}

ObservationBudget ModestBudget() {
  // Every field is non-zero on purpose. Zero means "unset" and is filled from
  // the process ceiling, so a builder that left one zero would be silently
  // asking for the ceiling and a budget test would prove nothing.
  ObservationBudget budget;
  budget.max_nodes = 500;
  budget.max_text_bytes = 64 * 1024;
  budget.max_total_bytes = 256 * 1024;
  budget.max_depth = 32;
  budget.max_frames = 8;
  budget.deadline_ms = 5000;
  return budget;
}

}  // namespace

BipRequestBuilder::BipRequestBuilder(DeterministicIdSource* ids,
                                     TaskId task_id,
                                     TabId tab_id,
                                     ActorLeaseId lease_id)
    : ids_(ids),
      task_id_(std::move(task_id)),
      tab_id_(std::move(tab_id)),
      lease_id_(std::move(lease_id)),
      policy_id_(ids->NextSensitivityPolicyId()) {
  CHECK(ids_) << "A request builder with no identifier source cannot mint the "
                 "task-side identifiers a policy engine would have issued.";
}

BipRequestBuilder::~BipRequestBuilder() = default;

ObservationPolicyGrant BipRequestBuilder::Grant(
    ObservationScope max_scope,
    std::vector<Origin> allowed_origins,
    bool may_include_child_frames) const {
  ObservationPolicyGrant grant;
  grant.budget = ModestBudget();
  grant.max_scope = max_scope;
  grant.allowed_origins = std::move(allowed_origins);
  grant.may_include_child_frames = may_include_child_frames;
  // A read-oriented research task observes nothing above the not-sensitive
  // class. Widening this is a policy decision with its own approval path, and
  // no test in this directory is entitled to make it silently.
  grant.max_sensitivity = Sensitivity::kNotSensitive;
  grant.allowed_adapters = {AdapterKind::kDom,      AdapterKind::kAccessibility,
                            AdapterKind::kMetadata, AdapterKind::kForms,
                            AdapterKind::kSite,     AdapterKind::kLayout};
  return grant;
}

ObservationRequest BipRequestBuilder::Observation(
    const FrameId& root_frame_id,
    ObservationScope scope) const {
  ObservationRequest request;
  request.request_id = ids_->NextRequestId();
  request.authority_subject = ObservationAuthoritySubject::ForTask(task_id_);
  request.tab_id = tab_id_;
  request.root_frame_id = root_frame_id;
  request.scope = scope;
  request.adapters = ReadOrientedAdapters();
  request.requested_fields = ReadOrientedFields();
  request.include_child_frames = false;
  request.budget = ModestBudget();
  request.sensitivity_policy_id = policy_id_;
  request.task_purpose = "parity and correctness suite observation";
  return request;
}

ObservationRequest BipRequestBuilder::FormObservation(
    const FrameId& root_frame_id,
    FormObservationRoot form_root) const {
  ObservationRequest request;
  request.request_id = ids_->NextRequestId();
  request.authority_subject = ObservationAuthoritySubject::ForTask(task_id_);
  request.tab_id = tab_id_;
  request.root_frame_id = root_frame_id;
  request.scope = ObservationScope::kSection;
  request.form_root = std::move(form_root);
  request.adapters = FormObservationAdapters();
  request.requested_fields = FormObservationFields();
  request.include_child_frames = false;
  request.budget = ModestBudget();
  request.sensitivity_policy_id = policy_id_;
  request.task_purpose = "parity and correctness suite exact form observation";
  return request;
}

SubscriptionRequest BipRequestBuilder::Subscription(
    const FrameId& frame_id,
    const PageEpoch& expected_page_epoch) const {
  SubscriptionRequest request;
  request.request_id = ids_->NextRequestId();
  request.task_id = task_id_;
  request.tab_id = tab_id_;
  request.frame_id = frame_id;
  request.expected_page_epoch = expected_page_epoch;
  request.scope = ObservationScope::kDocument;
  request.adapters = ReadOrientedAdapters();
  request.requested_fields = ReadOrientedFields();
  request.budget.max_queue_depth = 16;
  request.budget.max_queued_bytes = 128 * 1024;
  request.budget.coalescing_window_ms = 50;
  request.budget.max_delta_bytes = 64 * 1024;
  // Asked for on purpose: they are the first thing the backpressure ladder
  // gives up, so a subscription that never asked for them could not observe
  // the ladder's first rung at all.
  request.include_text_deltas = true;
  request.include_layout_deltas = true;
  request.sensitivity_policy_id = policy_id_;
  return request;
}

AuthorizedActionEnvelope BipRequestBuilder::Activate(
    const NodeHandle& handle,
    GraphRevision required_graph_revision,
    std::vector<Postcondition> expected_postconditions) const {
  CHECK(!expected_postconditions.empty())
      << "An envelope with no expected postcondition is refused before "
         "dispatch: an unverifiable action is not an authorized action. Use "
         "WithNoPostcondition() when that refusal is the property under test.";

  AuthorizedActionEnvelope envelope;
  envelope.schema_version = kBipSchemaVersion;
  envelope.dispatch_id = ids_->NextDispatchId();
  envelope.action_id = ids_->NextActionId();
  envelope.capability_reference = ids_->NextCapabilityReference();
  envelope.action_type = ActionType::kActivate;
  envelope.target_handle = handle;
  envelope.required_graph_revision = required_graph_revision;

  // The preconditions the stale-node algorithm re-checks. Every one of them is
  // browser-checkable, which is the reason each is here rather than trusted
  // from the renderer's reply.
  Precondition epoch;
  epoch.kind = PreconditionKind::kExactPageEpoch;
  epoch.page_epoch = handle.page_epoch;
  Precondition revision;
  revision.kind = PreconditionKind::kAcceptableGraphRevision;
  revision.min_graph_revision = required_graph_revision;
  Precondition origin;
  origin.kind = PreconditionKind::kExactOrigin;
  origin.origin = handle.expected_origin;
  Precondition active;
  active.kind = PreconditionKind::kDocumentActive;
  Precondition exists;
  exists.kind = PreconditionKind::kNodeExists;
  envelope.preconditions = {epoch, revision, origin, active, exists};

  envelope.expected_postconditions = std::move(expected_postconditions);
  envelope.absolute_deadline_monotonic_ms =
      DeterministicClock::DeadlineFromNow(kActionDeadline);

  envelope.task_id = task_id_;
  envelope.principal.kind = PrincipalKind::kAssistant;
  envelope.idempotency_policy = IdempotencyPolicy::kNonIdempotent;

  envelope.capability.capability_reference = envelope.capability_reference;
  envelope.capability.actor_lease_id = lease_id_;
  envelope.capability.expires_at_monotonic_ms =
      DeterministicClock::DeadlineFromNow(kCapabilityLifetime);
  envelope.capability.policy_version = 1;

  // Last, over the finished envelope. The digest answers "is this the action
  // that was authorized", so computing it before the envelope is finished would
  // make it answer a question about a different action.
  envelope.action_digest = ComputeActionDigest(envelope);
  return envelope;
}

AuthorizedBrowserCommand BipRequestBuilder::Navigate(
    const std::string& normalized_destination,
    std::vector<Postcondition> expected_postconditions) const {
  CHECK(!expected_postconditions.empty())
      << "A browser command with no expected postcondition is refused for the "
         "same reason a node action is.";

  AuthorizedBrowserCommand command;
  command.schema_version = kBipSchemaVersion;
  command.dispatch_id = ids_->NextDispatchId();
  command.action_id = ids_->NextActionId();
  command.capability_reference = ids_->NextCapabilityReference();
  command.command_type = BrowserCommandType::kNavigate;
  command.tab_id = tab_id_;
  command.argument = normalized_destination;
  command.expected_postconditions = std::move(expected_postconditions);
  command.absolute_deadline_monotonic_ms =
      DeterministicClock::DeadlineFromNow(kActionDeadline);
  command.task_id = task_id_;
  command.principal.kind = PrincipalKind::kAssistant;
  command.idempotency_policy = IdempotencyPolicy::kIdempotentWrite;
  command.capability.capability_reference = command.capability_reference;
  command.capability.actor_lease_id = lease_id_;
  command.capability.expires_at_monotonic_ms =
      DeterministicClock::DeadlineFromNow(kCapabilityLifetime);
  command.capability.policy_version = 1;
  command.action_digest = ComputeBrowserCommandDigest(command);
  return command;
}

// static
AuthorizedActionEnvelope BipRequestBuilder::WithTamperedTarget(
    AuthorizedActionEnvelope envelope,
    const SemanticNodeId& replacement_node) {
  // The digest is deliberately left as it was. That is the whole point: the
  // envelope now describes a different action from the one that was authorized,
  // and the ledger must refuse it before a capability is consumed.
  envelope.target_handle.node_id = replacement_node;
  return envelope;
}

// static
AuthorizedActionEnvelope BipRequestBuilder::WithUnknownLease(
    AuthorizedActionEnvelope envelope,
    const ActorLeaseId& unknown_lease) {
  envelope.capability.actor_lease_id = unknown_lease;
  envelope.action_digest = ComputeActionDigest(envelope);
  return envelope;
}

// static
AuthorizedActionEnvelope BipRequestBuilder::WithExpiredCapability(
    AuthorizedActionEnvelope envelope) {
  // Zero is unambiguously in the past on the monotonic scale, and it needs no
  // clock arithmetic that could itself be the thing that is wrong.
  envelope.capability.expires_at_monotonic_ms = 0;
  envelope.action_digest = ComputeActionDigest(envelope);
  return envelope;
}

// static
AuthorizedActionEnvelope BipRequestBuilder::WithNoPostcondition(
    AuthorizedActionEnvelope envelope) {
  envelope.expected_postconditions.clear();
  envelope.action_digest = ComputeActionDigest(envelope);
  return envelope;
}

// static
AuthorizedActionEnvelope BipRequestBuilder::Reseal(
    AuthorizedActionEnvelope envelope) {
  envelope.action_digest = ComputeActionDigest(envelope);
  return envelope;
}

}  // namespace taffy::test

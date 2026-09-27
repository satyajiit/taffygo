// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_BIP_OBSERVATION_H_
#define TAFFY_PUBLIC_BIP_OBSERVATION_H_

#include <stdint.h>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/common/public/page_inspector_projection.h"

// What the isolated core asks for and what it gets back (protocol sections 7
// and 10, taffy-core/contracts/bip/schema/snapshot.schema.json and
// delta.schema.json).
//
// The raw semantic graph does not appear in this header as C++ structs. Mojo
// performs the only deserialization; the browser validates bounds, frames an
// opaque payload for the isolated Rust core, and may retain only a separate
// redacted projection with fresh local identities for Core API. Rust remains
// the only layer that interprets page meaning or consumes the raw graph.
//
// The one exception is ResolvedNodeFacts: a handful of scalars the browser has
// to re-read to decide whether an action is still legal. It is small on
// purpose.

namespace taffy {

enum class AdapterRequirementLevel : uint8_t {
  kRequired = 0,
  kOptional = 1,
};

struct AdapterRequirement {
  AdapterKind adapter = AdapterKind::kDom;
  AdapterRequirementLevel requirement = AdapterRequirementLevel::kOptional;
};

enum class AdapterStatus : uint8_t {
  kOk = 0,
  kIncomplete = 1,
  kConflicted = 2,
  kUnsupported = 3,
  kFailed = 4,
};

struct AdapterReport {
  AdapterKind adapter = AdapterKind::kDom;
  AdapterStatus status = AdapterStatus::kUnsupported;
  uint32_t adapter_version = 0;
  uint32_t extraction_rule_version = 0;
  std::optional<std::string> detail_code;
};

// The exact live form subtree a SECTION observation may read. This is a
// read-only observation root, not an action handle: carrying it never enables
// filling, submitting, or a generic click. The renderer validates both fields
// before and after collection and refuses if the named node is no longer the
// same live form.
struct FormObservationRoot {
  SemanticNodeId node_id;
  GraphRevision minimum_graph_revision = 0;

  friend bool operator==(const FormObservationRoot&,
                         const FormObservationRoot&) = default;
};

// The exact live image or video node a document observation may read. This is
// observation authority only: it cannot be resolved as an action target.
struct MediaObservationRoot {
  SemanticNodeId node_id;
  GraphRevision minimum_graph_revision = 0;

  friend bool operator==(const MediaObservationRoot&,
                         const MediaObservationRoot&) = default;
};

enum class BudgetKind : uint8_t {
  kMaxNodes = 0,
  kMaxTextBytes = 1,
  kMaxTotalBytes = 2,
  kMaxDepth = 3,
  kMaxFrames = 4,
  kMaxMessageBytes = 5,
  kDeadline = 6,
};

// Which fields of a node a caller wants. Mirrors the contract's SemanticField.
enum class SemanticField : uint8_t {
  kRole = 0,
  kName = 1,
  kDescription = 2,
  kTextRuns = 3,
  kStates = 4,
  kValueDescriptor = 5,
  kDestination = 6,
  kBounds = 7,
  kActions = 8,
  kAttributes = 9,
  kSensitivity = 10,
  kEdges = 11,
  kFrames = 12,
  // Appended by the additive minor contract step recorded in
  // taffy-core/contracts/bip/schema/bip.version.json; the values above keep the
  // numbers they had, because a stored record already names them.
  kContentTrust = 13,
  kContentSignals = 14,
  kChallengeKind = 15,
};

enum class DocumentLifecycleState : uint8_t {
  kActive = 0,
  kSpeculative = 1,
  kPendingCommit = 2,
  kPrerendering = 3,
  kFrozen = 4,
  kBackForwardCached = 5,
  kCrashed = 6,
  kDestroyed = 7,
};

enum class GraphPayloadEncoding : uint8_t {
  // No graph travelled with this result: metadata only. This is what a
  // lifecycle-only observation returns, and what any endpoint returns when no
  // encoder is installed. Reporting it explicitly is how a caller tells the
  // difference between "this page has no semantic content" and "no encoder is
  // installed", which a zero-length payload would hide.
  kNone = 0,
  // The encoding generated from taffy-core/contracts/bip. The isolated Rust
  // core decodes it with the generated bip-types reader.
  kBipContract = 1,
};

// Browser/content-only authority identity for one observation. It never
// travels to a renderer. The explicit tag preserves the distinction between a
// task-owned observation and an explicit taskless user read all the way into
// the content-free audit record.
enum class ObservationAuthoritySubjectKind : uint8_t {
  kTask = 0,
  kDirectUserIntent = 1,
};

struct ObservationAuthoritySubject {
  ObservationAuthoritySubjectKind kind = ObservationAuthoritySubjectKind::kTask;
  TaskId task_id;
  DirectIntentId direct_intent_id;

  static ObservationAuthoritySubject ForTask(TaskId task_id) {
    ObservationAuthoritySubject subject;
    subject.task_id = std::move(task_id);
    return subject;
  }

  static ObservationAuthoritySubject ForDirectUserIntent(
      DirectIntentId direct_intent_id) {
    ObservationAuthoritySubject subject;
    subject.kind = ObservationAuthoritySubjectKind::kDirectUserIntent;
    subject.direct_intent_id = std::move(direct_intent_id);
    return subject;
  }

  bool is_valid() const {
    switch (kind) {
      case ObservationAuthoritySubjectKind::kTask:
        return task_id.is_valid() && !IsDirectIntentIdentifier(task_id.value) &&
               direct_intent_id.value.empty();
      case ObservationAuthoritySubjectKind::kDirectUserIntent:
        return task_id.value.empty() && direct_intent_id.is_valid();
    }
    return false;
  }

  const std::string& identifier() const {
    return kind == ObservationAuthoritySubjectKind::kDirectUserIntent
               ? direct_intent_id.value
               : task_id.value;
  }
};

struct ObservationRequest {
  RequestId request_id;
  // Browser-side addition. The contract's SnapshotRequest has no authority
  // subject because a renderer has no business knowing whether a task or an
  // explicit user intent requested the read. The tagged subject stays on this
  // side of the renderer boundary and is retained only in content-free audit.
  ObservationAuthoritySubject authority_subject;
  TabId tab_id;
  FrameId root_frame_id;
  // Unset on first bind. Set on refresh, and then it must match the broker's
  // current epoch or the request is rejected as stale rather than silently
  // rebound to the new document.
  std::optional<PageEpoch> expected_page_epoch;
  ObservationScope scope = ObservationScope::kViewport;
  // Present exactly for kSection, absent for every other scope.
  std::optional<FormObservationRoot> form_root;
  // Optional only for kDocument and mutually exclusive with form_root.
  std::optional<MediaObservationRoot> media_root;
  std::vector<AdapterRequirement> adapters;
  std::vector<SemanticField> requested_fields;
  bool include_child_frames = false;
  std::vector<Origin> allowed_origins;
  ObservationBudget budget;
  SensitivityPolicyId sensitivity_policy_id;
  std::string task_purpose;
};

struct TruncationSummary {
  bool truncated = false;
  std::vector<BudgetKind> budgets_reached;
  uint32_t omitted_node_count = 0;
  uint32_t omitted_text_bytes = 0;
  uint32_t omitted_frame_count = 0;
  bool may_change_answer = false;
};

struct RedactionSummary {
  uint32_t redacted_field_count = 0;
  uint32_t suppressed_secret_value_count = 0;
  uint32_t sensitive_zone_count = 0;
  uint32_t policy_filtered_frame_count = 0;
  Sensitivity highest_class_present = Sensitivity::kNotSensitive;
};

// One frame of the tree, as the browser owns it. Parentage comes from
// Chromium's frame tree, never from what a renderer claimed
// (protocol section 8.1).
struct FrameSummary {
  FrameId frame_id;
  std::optional<FrameId> parent_frame_id;
  PageEpoch page_epoch;
  GraphRevision graph_revision = 0;
  Origin origin;
  bool is_main_frame = false;
  bool is_out_of_process = false;
  bool is_cross_origin_to_parent = false;
  DocumentLifecycleState lifecycle_state = DocumentLifecycleState::kDestroyed;
  // False when the frame exists but its content was not extracted. A hidden
  // third-party frame is not included merely because it exists.
  bool included = false;
};

// Browser-process-only authority material for one observed link. Navigation
// and download consumers apply their own rules to the retained flags.
// The destination crosses the renderer boundary only so the trusted browser
// can bind a later opaque node handle to the exact address it observed. It is
// never serialized into the BIP graph payload, Core Service, a model request,
// an audit record, or the durable journal.
// The value is the URL maxLength in BIP snapshot.schema.json; the renderer
// repeats it locally because its DEPS boundary deliberately forbids importing
// this browser-side type surface.
inline constexpr size_t kMaxTransientObservedLinkDestinationBytes = 4096u;

struct TransientObservedLink {
  SemanticNodeId node_id;
  std::string normalized_destination;
  bool opens_new_tab = false;
  bool is_download = false;
};

// The single terminal result of one observation request.
struct ObservationEnvelope {
  std::string schema_version;
  RequestId request_id;
  ObservationResultCode code = ObservationResultCode::kInternalError;

  SnapshotId snapshot_id;
  ProfileId profile_id;
  BrowserWindowId browser_window_id;
  TabId tab_id;
  FrameId root_frame_id;
  PageEpoch page_epoch;
  GraphRevision graph_revision = 0;
  EventSequence event_sequence = 0;
  DocumentLifecycleState lifecycle_state = DocumentLifecycleState::kDestroyed;

  // Browser-owned, not renderer-reported. The broker fills these from its
  // committed navigation record and discards whatever the renderer echoed
  // (protocol section 7.2).
  UrlMetadata committed_url_metadata;
  Origin origin;
  bool is_potentially_trustworthy = false;
  bool is_incognito = false;

  MonotonicMillis capture_time_monotonic_ms = 0;
  ObservationScope scope = ObservationScope::kViewport;
  // Browser-authored echo of the exact validated kSection root. Never adopted
  // from a renderer reply.
  std::optional<FormObservationRoot> form_root;
  // Browser-authored echo of the exact validated media root.
  std::optional<MediaObservationRoot> media_root;
  std::vector<AdapterReport> adapters;
  std::vector<FrameSummary> frames;

  uint32_t node_count = 0;
  uint32_t total_bytes = 0;
  TruncationSummary truncation;
  RedactionSummary redaction;
  // Stable warning codes from the contract's WarningCode, carried as their
  // numeric member values. Never renderer-authored prose.
  std::vector<uint8_t> warning_codes;

  GraphPayloadEncoding encoding = GraphPayloadEncoding::kNone;
  std::vector<uint8_t> graph_payload;
  // Kept beside, never inside, the graph payload. A browser-owned registry
  // consumes this list synchronously and replaces it on every complete
  // snapshot. No isolated or model-facing consumer receives these addresses.
  std::vector<TransientObservedLink> transient_observed_links;
  // Browser-redacted UI staging data. It is never serialized into BIP or the
  // Core Service observation result; the direct selected-page broker consumes
  // it locally and projects it into generated Core API records.
  std::optional<InspectorGraphProjection> inspector_projection;
};

// The small set of scalars the browser re-reads before it allows an action
// (protocol section 12, step 6). Everything here is checked in the browser
// process; nothing here is trusted from the renderer without a corroborating
// browser-owned fact where one exists.
enum class ChallengeKind : uint8_t {
  kNone = 0,
  kImage = 1,
  kInteractive = 2,
  kOneTimeCode = 3,
};

struct ChallengeBounds {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;
};

struct ResolvedNodeFacts {
  SemanticNodeId node_id;
  // Browser-redacted accessible label for the trusted local form surface.
  // Transport-only: never serialized into a task or status payload.
  std::string display_label;
  GraphRevision observed_at_revision = 0;
  uint16_t role = 0;
  std::vector<ActionType> available_actions;
  // Asserted states only. A state that is absent is "not asserted", which
  // fails closed for every positive requirement.
  std::vector<NodeState> asserted_states;
  Sensitivity sensitivity = Sensitivity::kUnknownSensitive;
  // A renderer may assert only the four page-authored labels. The browser
  // converts every privileged or missing wire value to kUnknownUntrusted.
  ContentTrust content_trust = ContentTrust::kUnknownUntrusted;
  ChallengeKind challenge_kind = ChallengeKind::kNone;
  // Viewport-relative geometry of the structural challenge target, when the
  // renderer could still resolve it. It is a capture/highlight hint only and
  // is never accepted as action identity.
  std::optional<ChallengeBounds> challenge_bounds;
  std::vector<SemanticNodeId> form_field_node_ids;
  std::optional<Destination> destination;
  std::optional<ContentDigest> value_digest;
  GraphRevision value_changed_at_revision = 0;

  // Written as loops rather than std::ranges so this header keeps its
  // zero-include property; both lists are single digits long in practice.
  bool HasState(NodeState state) const {
    for (NodeState candidate : asserted_states) {
      if (candidate == state) {
        return true;
      }
    }
    return false;
  }

  bool SupportsAction(ActionType action) const {
    for (ActionType candidate : available_actions) {
      if (candidate == action) {
        return true;
      }
    }
    return false;
  }
};

// Why previously held handles stopped being usable. Members and order match
// the contract's InvalidationReason exactly (delta.schema.json).
//
// It lives here rather than in the browser broker because it is part of what
// crosses to the core service: a consumer that has to switch on the reason
// needs the enumeration, and carrying it as a bare integer would mean every
// consumer wrote its own table of what the integers meant.
enum class InvalidationCode : uint8_t {
  kCrossDocumentCommit = 0,
  kHistoryRouteChange = 1,
  kChildFrameNavigation = 2,
  kOriginChanged = 3,
  kPrerenderActivation = 4,
  kBfcacheEntered = 5,
  kBfcacheRestored = 6,
  kSequenceGap = 7,
  kDeltaOverflow = 8,
  kUnknownDeltaField = 9,
  kAdapterRestart = 10,
  kBrokerInvalidation = 11,
  kMemoryPressure = 12,
  kRendererCrashed = 13,
  kEndpointDisconnected = 14,
  kTabClosed = 15,
  kProfileTeardown = 16,
  // Added at protocol 0.3, mirroring mojom::InvalidationReason. Kept distinct
  // from kEndpointDisconnected and kRendererCrashed on purpose: a frame leaving
  // the tree is ordinary lifecycle, and a subscriber that cannot tell it from a
  // dead renderer cannot report the difference.
  kFrameDetached = 17,
};

// True when the reason means the document itself is gone, as opposed to the
// stream having lost its place inside a document that is still there. The
// distinction decides whether a subscriber may resnapshot the same epoch or
// has to bind a new one, so it is written once.
constexpr bool InvalidationRetiresDocument(InvalidationCode code) {
  switch (code) {
    case InvalidationCode::kCrossDocumentCommit:
    case InvalidationCode::kOriginChanged:
    case InvalidationCode::kPrerenderActivation:
    case InvalidationCode::kBfcacheEntered:
    case InvalidationCode::kBfcacheRestored:
    case InvalidationCode::kRendererCrashed:
    case InvalidationCode::kEndpointDisconnected:
    case InvalidationCode::kTabClosed:
    case InvalidationCode::kProfileTeardown:
      return true;
    case InvalidationCode::kHistoryRouteChange:
    case InvalidationCode::kChildFrameNavigation:
    case InvalidationCode::kFrameDetached:
    case InvalidationCode::kSequenceGap:
    case InvalidationCode::kDeltaOverflow:
    case InvalidationCode::kUnknownDeltaField:
    case InvalidationCode::kAdapterRestart:
    case InvalidationCode::kBrokerInvalidation:
    case InvalidationCode::kMemoryPressure:
      return false;
  }
  // An unrecognized reason retires the document. Fail closed: the safe
  // mistake is binding a new epoch that was not strictly necessary.
  return true;
}

// Delivered out of band when previously held handles stop being usable
// (protocol section 13). It is not a reply to a request: it can arrive at any
// time, and it takes priority over queued extraction and action work
// (protocol section 6.3).
struct InvalidationNotice {
  std::string schema_version;
  std::optional<SubscriptionId> subscription_id;
  TabId tab_id;
  FrameId frame_id;
  PageEpoch page_epoch;
  EventSequence event_sequence = 0;
  InvalidationCode reason = InvalidationCode::kBrokerInvalidation;
  bool retires_page_epoch = true;
  bool invalidates_child_frames_only = false;
  bool resnapshot_required = true;
  std::optional<PageEpoch> new_page_epoch;
  MonotonicMillis observed_at_monotonic_ms = 0;
};

// BackpressureNotice used to live here. It moved to bip_subscription.h when
// the subscription path was implemented: it is a statement about one standing
// subscription, not about an observation, and its well-formedness rules depend
// on the delta classes in bip_delta.h. Nothing in this header should grow a
// second opinion about backpressure.

}  // namespace taffy

#endif  // TAFFY_PUBLIC_BIP_OBSERVATION_H_

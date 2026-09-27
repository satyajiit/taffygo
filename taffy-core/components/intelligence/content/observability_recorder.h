// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVABILITY_RECORDER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVABILITY_RECORDER_H_

#include <stdint.h>

#include <array>
#include <string_view>
#include <type_traits>

#include "base/memory/raw_ptr.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_delta.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/common/public/bip_subscription.h"

// Privacy-preserving observability (protocol section 16).
//
// The rule is short: production analytics must not contain raw DOM, semantic
// text, form values, accessible names, full URLs or query strings,
// screenshots, model prompts, model outputs, credentials, or persistent
// cross-site node identifiers.
//
// Rules written in comments get broken. This one is enforced by the type
// system instead: both record structs below are asserted to be trivially
// copyable, which makes it impossible to add a std::string, a std::vector or
// any other owning field to them. Adding "just the page title, for debugging"
// stops being a code-review question and becomes a compile error. Identifiers
// are carried in fixed-capacity buffers for the same reason — bounded by the
// contract's own identifier length, and incapable of holding a page.
//
// Everything else here is enums, counts and durations. The only origin
// information is a category, plus an optional keyed diagnostic hash that is
// emitted only when policy permits it and is not stable across sessions, so it
// cannot become a persistent cross-site identifier.
//
// Content debugging is a separate, explicitly opted-in path with its own
// retention and deletion story (protocol section 16). It does not run through
// this class.

namespace taffy {

// A bounded copy of an opaque identifier. Fixed capacity so the record stays
// trivially copyable; the capacity is the contract's own identifier bound.
struct RecordIdentifier {
  std::array<char, kMaxIdentifierChars + 1> chars = {};
};

// Copies at most kMaxIdentifierChars characters. A longer value is truncated
// rather than rejected: this is a diagnostic record, and a truncated
// identifier is still a better correlation key than none.
RecordIdentifier ToRecordIdentifier(std::string_view value);

enum class OriginCategory : uint8_t {
  kUnknown = 0,
  kSameOriginAsTab = 1,
  kSameSiteAsTab = 2,
  kCrossSite = 3,
  kOpaque = 4,
};

// Why an action could not use the handle it was given, as a category rather
// than as a message. Feeds the stale-rate metric SP-04 needs.
enum class StaleReason : uint8_t {
  kNone = 0,
  kPageEpoch = 1,
  kGraphRevision = 2,
  kNodeGone = 3,
  kOrigin = 4,
  kRoleOrAction = 5,
  kDestination = 6,
  kLifecycle = 7,
};

enum class VerifierOutcome : uint8_t {
  kNotAttempted = 0,
  kCorroborated = 1,
  kContradicted = 2,
  kTimedOut = 3,
  kCancelled = 4,
};

enum class ObservationRecordAuthoritySubjectKind : uint8_t {
  kTask = 0,
  kDirectUserIntent = 1,
};

struct ObservationRecord {
  RecordIdentifier request_id;
  ObservationRecordAuthoritySubjectKind authority_subject_kind =
      ObservationRecordAuthoritySubjectKind::kTask;
  RecordIdentifier authority_subject_id;
  RecordIdentifier tab_id;
  RecordIdentifier frame_id;
  // Epoch and revision transitions, without any page text.
  RecordIdentifier page_epoch;
  uint64_t graph_revision = 0;

  uint16_t bip_version_major = 0;
  uint16_t bip_version_minor = 0;
  uint32_t extraction_rule_version = 0;
  uint32_t policy_version = 0;

  ObservationResultCode code = ObservationResultCode::kInternalError;
  OriginCategory origin_category = OriginCategory::kUnknown;

  uint32_t node_count = 0;
  uint32_t byte_count = 0;
  uint32_t truncated_node_count = 0;
  uint32_t redacted_field_count = 0;
  uint32_t suppressed_secret_value_count = 0;
  uint32_t adapters_requested = 0;
  uint32_t adapters_unavailable = 0;

  int64_t latency_us = 0;
  bool cancelled = false;
  bool truncated = false;

  // Zero unless policy permits a keyed local diagnostic hash. Keyed per
  // session, so it cannot link a user across sessions or across sites.
  uint64_t origin_diagnostic_hash = 0;
};

// Copies only the tagged opaque authority identity into content-free audit.
// Returns false for conditional-presence violations; in particular a direct
// intent can never be recorded in a task slot because ObservationRecord has no
// legacy task slot.
bool PopulateObservationRecordAuthority(
    const ObservationAuthoritySubject& subject,
    ObservationRecord* record);

struct ActionRecord {
  RecordIdentifier request_id;
  RecordIdentifier dispatch_id;
  RecordIdentifier task_id;
  RecordIdentifier action_id;
  RecordIdentifier tab_id;
  RecordIdentifier frame_id;
  RecordIdentifier page_epoch;
  RecordIdentifier capability_reference;
  RecordIdentifier actor_lease_id;
  uint64_t graph_revision = 0;

  ActionType action_type = ActionType::kActivate;
  // False for a browser-owned command, where action_type carries no meaning.
  bool is_node_action = true;
  ActionResultCode code = ActionResultCode::kInternalError;
  PreconditionKind failed_precondition = PreconditionKind::kNodeExists;
  bool had_failed_precondition = false;
  PostconditionKind verified_postcondition = PostconditionKind::kNoMutation;
  bool had_verified_postcondition = false;
  StaleReason stale_reason = StaleReason::kNone;
  VerifierOutcome verifier_outcome = VerifierOutcome::kNotAttempted;
  OriginCategory origin_category = OriginCategory::kUnknown;

  uint32_t policy_version = 0;
  int64_t dispatch_latency_us = 0;
  int64_t verification_latency_us = 0;
  bool renderer_acknowledged = false;
  bool repeat_may_duplicate_effect = true;
};

// What happened to one delta subscription. Named rather than inferred from
// counter movement, because "the projection died" and "the subscriber is
// behind" need different answers and a rate of change cannot tell them apart.
enum class SubscriptionEventKind : uint8_t {
  kOpened = 0,
  kDeltaDelivered = 1,
  // Applied nothing and the projection survived: a duplicate or a reordered
  // arrival.
  kDeltaDiscarded = 2,
  // The projection is dead and a fresh snapshot is the only way back.
  kProjectionInvalidated = 3,
  kBackpressure = 4,
  kResnapshotRebased = 5,
  kClosed = 6,
};

// One delta-stream event. Content free on exactly the same terms as the two
// records above: identifiers, enums and counts, and the static assertion below
// is what keeps it that way.
//
// The node counts are counts. A delta's added and changed nodes never appear
// here in any form, and the removed identifiers are counted rather than listed,
// because a list of node identifiers gathered across sessions is the persistent
// cross-site identifier protocol section 16 forbids.
struct SubscriptionRecord {
  RecordIdentifier request_id;
  RecordIdentifier subscription_id;
  RecordIdentifier task_id;
  RecordIdentifier tab_id;
  RecordIdentifier frame_id;
  RecordIdentifier page_epoch;

  // The transition, not the content: where the projection was and where the
  // delta moved it to.
  uint64_t from_revision = 0;
  uint64_t to_revision = 0;
  uint64_t event_sequence = 0;

  SubscriptionEventKind event = SubscriptionEventKind::kOpened;
  SubscriptionState state = SubscriptionState::kActive;
  DeltaRejectReason reject_reason = DeltaRejectReason::kNone;
  BackpressureAction backpressure_action = BackpressureAction::kCoalesced;
  bool had_backpressure = false;
  bool resnapshot_required = false;

  uint32_t queue_depth = 0;
  uint32_t queued_bytes = 0;
  uint32_t byte_count = 0;
  uint32_t added_node_count = 0;
  uint32_t changed_node_count = 0;
  uint32_t removed_node_count = 0;
  uint32_t coalesced_mutation_count = 0;

  // Cumulative for the subscription, so a single record is enough to see
  // whether a stream has been struggling all along or only just started to.
  uint32_t delivered_count = 0;
  uint32_t dropped_delta_count = 0;
  uint32_t gap_count = 0;
  uint32_t duplicate_count = 0;
  uint32_t out_of_order_count = 0;
  uint32_t late_after_terminal_count = 0;

  int64_t latency_us = 0;
  OriginCategory origin_category = OriginCategory::kUnknown;
};

// The type-level enforcement described above. If a future change adds an
// owning member to either struct, the build stops here with a message that
// names the rule.
static_assert(std::is_trivially_copyable_v<ObservationRecord>,
              "Observability records carry identifiers, enums and counts only. "
              "An owning member such as std::string would allow page content "
              "into analytics; protocol section 16 forbids it.");
static_assert(std::is_trivially_copyable_v<ActionRecord>,
              "Observability records carry identifiers, enums and counts only. "
              "An owning member such as std::string would allow page content "
              "into analytics; protocol section 16 forbids it.");
static_assert(std::is_trivially_copyable_v<SubscriptionRecord>,
              "Observability records carry identifiers, enums and counts only. "
              "A vector of removed node identifiers would be a persistent "
              "cross-site identifier; protocol section 16 forbids it.");

// The local structured store. The browser owns its lifetime and forwards
// content-free records through the audited storage boundary.
class ObservabilitySink {
public:
  virtual ~ObservabilitySink() = default;
  virtual void RecordObservation(const ObservationRecord &record) = 0;
  virtual void RecordAction(const ActionRecord &record) = 0;
  virtual void RecordSubscription(const SubscriptionRecord &record) = 0;
};

class ObservabilityRecorder {
public:
  ObservabilityRecorder();
  ObservabilityRecorder(const ObservabilityRecorder &) = delete;
  ObservabilityRecorder &operator=(const ObservabilityRecorder &) = delete;
  ~ObservabilityRecorder();

  // Null disables recording. Nothing is buffered while there is no sink:
  // holding records until one appears would be a place for data to live longer
  // than intended.
  void SetSink(ObservabilitySink *sink);

  void Record(const ObservationRecord &record);
  void Record(const ActionRecord &record);
  void Record(const SubscriptionRecord &record);

  // Derives the category without keeping the origin. Used at the one call site
  // that has both origins in hand.
  static OriginCategory CategorizeOrigin(bool same_origin_as_tab,
                                         bool same_site_as_tab, bool is_opaque);

  // Maps a terminal action code to its stale category, so the mapping exists
  // once rather than at every call site.
  static StaleReason StaleReasonFor(ActionResultCode code);

private:
  // The sink is owned elsewhere and outlives this recorder; raw_ptr because
  // //taffy compiles with the raw-pointer plugin enabled, which
  // rejects a bare pointer field.
  raw_ptr<ObservabilitySink> sink_ = nullptr;
};

} // namespace taffy

#endif // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVABILITY_RECORDER_H_

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/observability_recorder.h"

#include <algorithm>

namespace taffy {

RecordIdentifier ToRecordIdentifier(std::string_view value) {
  RecordIdentifier out;
  const size_t copied = std::min(value.size(), kMaxIdentifierChars);
  // std::copy_n rather than std::memcpy: the copy is bounds-correct either
  // way - `copied` is capped at kMaxIdentifierChars and `chars` holds one more
  // than that - but a raw libc call is rejected outright under
  // -Wunsafe-buffer-usage-in-libc-call, which is an error in the sanitizer
  // profiles this component is built in. The iterator form states the same
  // copy with the bound attached to it.
  std::copy_n(value.begin(), copied, out.chars.begin());
  out.chars[copied] = '\0';
  return out;
}

bool PopulateObservationRecordAuthority(
    const ObservationAuthoritySubject& subject,
    ObservationRecord* record) {
  if (!record || !subject.is_valid()) {
    return false;
  }
  record->authority_subject_kind =
      subject.kind == ObservationAuthoritySubjectKind::kDirectUserIntent
          ? ObservationRecordAuthoritySubjectKind::kDirectUserIntent
          : ObservationRecordAuthoritySubjectKind::kTask;
  record->authority_subject_id = ToRecordIdentifier(subject.identifier());
  return true;
}

ObservabilityRecorder::ObservabilityRecorder() = default;
ObservabilityRecorder::~ObservabilityRecorder() = default;

void ObservabilityRecorder::SetSink(ObservabilitySink* sink) {
  sink_ = sink;
}

void ObservabilityRecorder::Record(const ObservationRecord& record) {
  if (sink_) {
    sink_->RecordObservation(record);
  }
}

void ObservabilityRecorder::Record(const SubscriptionRecord& record) {
  if (sink_) {
    sink_->RecordSubscription(record);
  }
}

void ObservabilityRecorder::Record(const ActionRecord& record) {
  if (sink_) {
    sink_->RecordAction(record);
  }
}

// static
OriginCategory ObservabilityRecorder::CategorizeOrigin(bool same_origin_as_tab,
                                                       bool same_site_as_tab,
                                                       bool is_opaque) {
  if (is_opaque) {
    return OriginCategory::kOpaque;
  }
  if (same_origin_as_tab) {
    return OriginCategory::kSameOriginAsTab;
  }
  if (same_site_as_tab) {
    return OriginCategory::kSameSiteAsTab;
  }
  return OriginCategory::kCrossSite;
}

// static
StaleReason ObservabilityRecorder::StaleReasonFor(ActionResultCode code) {
  switch (code) {
    case ActionResultCode::kStalePageEpoch:
      return StaleReason::kPageEpoch;
    case ActionResultCode::kStaleGraph:
      return StaleReason::kGraphRevision;
    case ActionResultCode::kNodeGone:
      return StaleReason::kNodeGone;
    case ActionResultCode::kOriginChanged:
      return StaleReason::kOrigin;
    case ActionResultCode::kRoleOrActionChanged:
      return StaleReason::kRoleOrAction;
    case ActionResultCode::kDestinationChanged:
      return StaleReason::kDestination;
    case ActionResultCode::kDocumentInactive:
    case ActionResultCode::kTabGone:
    case ActionResultCode::kFrameGone:
      return StaleReason::kLifecycle;

    default:
      return StaleReason::kNone;
  }
}

}  // namespace taffy

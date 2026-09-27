// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// How an observation that produced no reading is settled.
//
// A deadline, a cancellation and a navigation that invalidated the request all
// arrive here, and none of them has a snapshot behind it. What the envelope
// still carries is the identity the request wrote down when it opened: a
// failure that names no document reads downstream as a result that does not
// match its request, which is a different and much worse thing to say
// (decision 0177).

#include <utility>

// LOG() is used below. Every sibling in this directory that logs includes
// this; this one did not, and only the sanitizer profile noticed, because
// it is the modules build: without `-fmodules` the declaration leaks in
// through page_intelligence_service_impl.h and the dev profiles compile.
#include "base/logging.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/monotonic_clock.h"
#include "taffy/components/intelligence/content/page_intelligence_service_impl.h"

namespace taffy {

ObservationEnvelope PageIntelligenceServiceImpl::BuildBareEnvelope(
    const RequestId& request_id,
    ObservationResultCode code) const {
  ObservationEnvelope envelope;
  envelope.schema_version = kBipSchemaVersion;
  envelope.request_id = request_id;
  envelope.tab_id = broker_->tab_id();
  envelope.code = code;
  envelope.capture_time_monotonic_ms = NowMonotonicMs();
  // The document this failure is about, from the record the request opened.
  // There is no snapshot, so there is nothing here that came from a renderer;
  // every value is one the browser already wrote down. Without them a failed
  // observation named no document at all, which read downstream as a result
  // that did not match its request (decision 0177).
  const auto pending = pending_observations_.find(request_id);
  if (pending != pending_observations_.end()) {
    envelope.root_frame_id = pending->second.clamped.root_frame_id;
    envelope.page_epoch = pending->second.expected_epoch;
    envelope.scope = pending->second.clamped.scope;
  }
  return envelope;
}

void PageIntelligenceServiceImpl::RefuseObservation(
    const RequestId& request_id,
    ObservationResultCode code,
    const char* at) {
  // A refusal decided before the request left this process. It never had a
  // pending entry, so it cannot collide with a reply or a deadline, and it
  // delivers its one terminal result directly.
  //
  // Content-free by construction: a compiled-in branch name and a closed code.
  LOG(WARNING) << "[taffy_observation_refused] at=" << at
               << " code=" << static_cast<int>(code);
  if (sink_) {
    sink_->OnObservationResult(BuildBareEnvelope(request_id, code));
  }
}

void PageIntelligenceServiceImpl::FinishObservationWithCode(
    RequestId request_id,
    ObservationResultCode code) {
  // Only ever called for a request that is in flight — by the deadline, by
  // Cancel, or by a navigation invalidation. An entry that is already gone
  // means something else settled first, and exactly one result per request id
  // means this call does nothing.
  if (!pending_observations_.contains(request_id)) {
    return;
  }
  FinishObservation(request_id, BuildBareEnvelope(request_id, code));
}

}  // namespace taffy

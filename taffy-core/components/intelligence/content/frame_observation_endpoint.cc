// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/frame_observation_endpoint.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "content/public/browser/render_frame_host.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"

namespace taffy {

FrameObservationEndpoint::FrameObservationEndpoint(
    content::RenderFrameHost* render_frame_host,
    FrameId frame_id,
    PageEpoch page_epoch,
    Origin origin)
    : content::DocumentUserData<FrameObservationEndpoint>(render_frame_host),
      frame_id_(std::move(frame_id)),
      page_epoch_(std::move(page_epoch)),
      origin_(std::move(origin)) {
  CHECK(frame_id_.is_valid());
  CHECK(page_epoch_.is_valid());
}

FrameObservationEndpoint::~FrameObservationEndpoint() = default;

void FrameObservationEndpoint::Retire() {
  is_actionable_ = false;
  // Dropping the remote cancels every in-flight call: their reply callbacks
  // run with the connection-error path, which is how unresolved results
  // become terminal instead of leaking (spec section 15).
  remote_.reset();
  protocol_info_.reset();
}

void FrameObservationEndpoint::NoteReportedRevision(GraphRevision revision) {
  if (revision > last_reported_revision_) {
    last_reported_revision_ = revision;
  }
}

void FrameObservationEndpoint::NoteSnapshotObserved(
    GraphRevision revision,
    EventSequence event_sequence) {
  NoteReportedRevision(revision);
  if (resnapshot_required_ &&
      event_sequence > resnapshot_required_at_sequence_) {
    resnapshot_required_ = false;
    resnapshot_required_at_sequence_ = 0;
  }
}

void FrameObservationEndpoint::RequireResnapshot() {
  // Already standing. Re-raising would move the watermark forward past
  // messages that have arrived since, and the flag would then outlive the
  // mutation that raised it.
  if (resnapshot_required_) {
    return;
  }
  resnapshot_required_ = true;
  resnapshot_required_at_sequence_ = sequence_.last_accepted();
}

EventSequenceTracker::Verdict FrameObservationEndpoint::ClassifyEventSequence(
    EventSequence event_sequence) {
  return sequence_.Classify(event_sequence);
}

void FrameObservationEndpoint::RecordLateReply() {
  sequence_.RecordLateAfterTerminal();
}

mojo::AssociatedRemote<mojom::PageIntelligence>&
FrameObservationEndpoint::remote() {
  if (!is_actionable_) {
    remote_.reset();
    return remote_;
  }
  if (remote_.is_bound()) {
    return remote_;
  }
  content::RenderFrameHost& rfh = render_frame_host();
  if (!rfh.IsRenderFrameLive()) {
    return remote_;
  }

  // VERIFY AT SP-04: the interface flavor is [Open (OD-027)]. Binding it
  // channel-associated is the conservative choice because the stale-node
  // algorithm depends on observation and action messages keeping their order
  // relative to navigation IPCs; an ordinary interface would let an action
  // overtake a commit. Confirm at the pinned milestone that
  // RenderFrameHost::GetRemoteAssociatedInterfaces() is still the supported
  // accessor and that a per-document associated interface is bound in the
  // renderer through the RenderFrame's associated interface registry.
  rfh.GetRemoteAssociatedInterfaces()->GetInterface(&remote_);
  remote_.set_disconnect_handler(
      base::BindOnce(&FrameObservationEndpoint::OnRemoteDisconnected,
                     base::Unretained(this)));
  return remote_;
}

void FrameObservationEndpoint::OnRemoteDisconnected() {
  // A mojo disconnect invalidates endpoint state and every unresolved node
  // handle (spec section 15). The broker learns about the underlying cause
  // through RenderFrameDeleted or the process-gone observer and emits the
  // invalidation; retiring here makes sure nothing is dispatched in the
  // window between the two.
  Retire();
}

void FrameObservationEndpoint::set_protocol_info(mojom::ProtocolInfoPtr info) {
  protocol_info_ = std::move(info);
}

DOCUMENT_USER_DATA_KEY_IMPL(FrameObservationEndpoint);

}  // namespace taffy

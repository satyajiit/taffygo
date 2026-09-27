// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_FRAME_OBSERVATION_ENDPOINT_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_FRAME_OBSERVATION_ENDPOINT_H_

#include <stdint.h>

#include "taffy/components/intelligence/content/event_sequence_tracker.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/common/public/bip_identity.h"
#include "content/public/browser/document_user_data.h"
#include "mojo/public/cpp/bindings/associated_remote.h"

namespace content {
class RenderFrameHost;
}  // namespace content

// One BIP endpoint per document (spec section 8.1: "each local or
// out-of-process frame has its own endpoint, origin, epoch, revision, node
// namespace, and limits").
//
// Why DocumentUserData and not WebContentsUserData or a map keyed by
// RenderFrameHost: a page epoch must die with its document. Hanging this off
// the document makes that automatic rather than something the invalidation
// code has to remember, which is the difference between a lifetime bug and no
// lifetime bug. The frame identity that outlives a document — FrameId, keyed
// by frame tree node — lives in the broker instead, because that is what
// survives a same-frame navigation.
//
// This object holds no page content. It holds identity, a revision counter,
// an ordering counter and a mojo remote.

namespace taffy {

class FrameObservationEndpoint
    : public content::DocumentUserData<FrameObservationEndpoint> {
 public:
  ~FrameObservationEndpoint() override;

  const FrameId& frame_id() const { return frame_id_; }
  const PageEpoch& page_epoch() const { return page_epoch_; }
  const Origin& origin() const { return origin_; }

  // The newest graph revision the renderer has stated for this document, and
  // the only revision number this process has. Zero until the renderer has
  // reported one, which is why it means "no revision" everywhere.
  //
  // There used to be a second counter here — browser-owned, starting at 1,
  // advanced whenever the browser learned of a mutation — and it was compared
  // against revisions that came out of the renderer. Two numbers in two
  // processes counting different things, compared as if they were one: the
  // browser's stood at 1 while the renderer's reached 97, so every freshness
  // test refused, and it refused before any renderer round trip, which is what
  // made it look like a stale page rather than an arithmetic error. A revision
  // is a statement about a document's graph, the renderer is the only party
  // that can make that statement, and "the browser knows this moved" is a
  // different fact that now has its own name below.
  GraphRevision last_reported_revision() const {
    return last_reported_revision_;
  }

  // True when the browser has learned of a mutation that can invalidate a
  // previously observed node, relationship, precondition or value (spec
  // section 5.3), and no renderer report has arrived since. It is a separate
  // question from the revision floor and it deserves its own answer: a handle
  // can be perfectly current by revision and still be describing a document
  // that moved a millisecond ago.
  bool resnapshot_required() const { return resnapshot_required_; }

  // False once the broker has retired this endpoint: the document entered the
  // back/forward cache, its renderer went away, or the tab is closing. A
  // retired endpoint answers no requests and dispatches no actions, and it is
  // never un-retired — a restored document gets a fresh epoch instead.
  bool is_actionable() const { return is_actionable_; }
  void Retire();

  // Records what the renderer said, from any message that states a revision.
  // Monotonic within the epoch: a lower number is a reordered message rather
  // than a rewind, and it is ignored rather than adopted. It does not clear
  // `resnapshot_required` — a delta describes changes to a projection, and the
  // flag is a statement that the projection itself needs rebuilding.
  void NoteReportedRevision(GraphRevision revision);

  // The same, for the one message that does answer the flag. `event_sequence`
  // and not `revision` decides whether it clears, because the mutation the
  // browser saw need not have changed the graph at all — a History API route
  // change is the standing example, and a snapshot taken after it can honestly
  // report the revision it reported before. What matters is whether this
  // snapshot could have seen the move, and per-epoch ordering is the only
  // thing that answers that.
  void NoteSnapshotObserved(GraphRevision revision,
                            EventSequence event_sequence);

  // The browser learned of a mutation that invalidates what was observed
  // before it — a same-document commit, for instance. It does not invent a
  // revision to express that, because it has no standing to: it records the
  // fact and lets the next snapshot settle it.
  void RequireResnapshot();

  // Classifies one renderer message's per-epoch sequence number. Every message
  // from this endpoint — snapshot reply, delta, invalidation — goes through the
  // same counter, because there is one ordering per epoch and two counters
  // would disagree about where it is.
  //
  // A gap is not a warning. It invalidates delta state and forces a fresh
  // snapshot (protocol section 6.3), and the caller is expected to act on it.
  EventSequenceTracker::Verdict ClassifyEventSequence(
      EventSequence event_sequence);

  // Records that a renderer reply arrived after its request had already
  // produced a terminal result. Late replies are ignored and counted
  // (protocol section 6.3); this is the counting.
  void RecordLateReply();

  const EventSequenceTracker::Counters& sequence_counters() const {
    return sequence_.counters();
  }

  // The renderer endpoint. Lazily bound; null when the renderer is not live.
  mojo::AssociatedRemote<mojom::PageIntelligence>& remote();

  // Cached negotiation result (spec section 6.2). Null until
  // GetProtocolInfo() has answered; the broker returns UNSUPPORTED rather
  // than guessing while it is null.
  const mojom::ProtocolInfoPtr& protocol_info() const {
    return protocol_info_;
  }
  void set_protocol_info(mojom::ProtocolInfoPtr info);

 private:
  friend class content::DocumentUserData<FrameObservationEndpoint>;

  FrameObservationEndpoint(content::RenderFrameHost* render_frame_host,
                           FrameId frame_id,
                           PageEpoch page_epoch,
                           Origin origin);

  void OnRemoteDisconnected();

  const FrameId frame_id_;
  const PageEpoch page_epoch_;
  const Origin origin_;

  // Zero always means "no revision", which keeps an uninitialized handle from
  // accidentally satisfying a revision floor.
  GraphRevision last_reported_revision_ = 0;
  bool resnapshot_required_ = false;
  // The last accepted event sequence when `resnapshot_required_` was raised.
  // Only a snapshot strictly newer than this clears the flag, which is what
  // keeps a reply already in flight from clearing a requirement it could not
  // have seen.
  EventSequence resnapshot_required_at_sequence_ = 0;
  EventSequenceTracker sequence_;
  bool is_actionable_ = true;

  mojo::AssociatedRemote<mojom::PageIntelligence> remote_;
  mojom::ProtocolInfoPtr protocol_info_;

  // VERIFY AT SP-01: whether DOCUMENT_USER_DATA_KEY_DECL() still exists at
  // the pinned milestone. Upstream removed the matching WebContentsUserData
  // declaration macro when the key moved into the template; if the same
  // cleanup has reached DocumentUserData, delete this line and keep only
  // DOCUMENT_USER_DATA_KEY_IMPL in the .cc.
  DOCUMENT_USER_DATA_KEY_DECL();
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_FRAME_OBSERVATION_ENDPOINT_H_

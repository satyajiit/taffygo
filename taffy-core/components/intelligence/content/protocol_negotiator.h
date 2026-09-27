// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_PROTOCOL_NEGOTIATOR_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_PROTOCOL_NEGOTIATOR_H_

#include <map>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "taffy/components/intelligence/content/renderer_call_deadline.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_protocol_support.h"
#include "mojo/public/cpp/bindings/associated_remote.h"

// Protocol negotiation, and the cache of what each endpoint said
// (protocol section 6.2).
//
// "ProtocolInfo reports supported adapters, observation scopes, action types,
// redaction features, maximum message size, and limits. Snapshot/action
// requests name required capabilities. Missing required support returns
// UNSUPPORTED, not a degraded result that looks complete."
//
// So negotiation is not a nicety, it is what makes UNSUPPORTED answerable
// before an extraction rather than after one. Three rules shape this class:
//
//   * An endpoint that cannot answer is unsupported, never assumed. There is
//     no default ProtocolInfo anywhere in this file.
//   * A wire value this build cannot map is dropped and counted, never coerced
//     to a neighbouring member. The counts travel in the envelope so that a
//     version skew is visible instead of silent.
//   * A recognized action type is retained exactly as advertised. Support is
//     only one fact: the dispatcher still requires an exact operation/input
//     pairing, live node action, sensitivity clearance, lease, capability and
//     durable intent before it can execute a write.
//
// The cache is keyed by frame and dropped when the frame's epoch is retired:
// negotiation belongs to a document, and keeping a retired document's answer
// would let a request name a required adapter the new document's endpoint
// never claimed to have.
//
// UI thread only.

namespace taffy {

class ProtocolNegotiator {
 public:
  using CompletionCallback =
      base::RepeatingCallback<void(ProtocolSupportEnvelope)>;

  // `on_settled` receives exactly one envelope per Query call.
  explicit ProtocolNegotiator(CompletionCallback on_settled);
  ProtocolNegotiator(const ProtocolNegotiator&) = delete;
  ProtocolNegotiator& operator=(const ProtocolNegotiator&) = delete;
  ~ProtocolNegotiator();

  // Asks `remote` what it supports. The call is bounded: a renderer that never
  // answers still produces a terminal result at `deadline`.
  void Query(const RequestId& request_id,
             const TabId& tab_id,
             const FrameId& frame_id,
             mojo::AssociatedRemote<mojom::PageIntelligence>& remote,
             base::TimeDelta deadline);

  // Terminal, with a cancellation code. No-op for a request that already
  // settled.
  void CancelWithCode(const RequestId& request_id, ObservationResultCode code);

  bool IsPending(const RequestId& request_id) const;

  // What this frame's endpoint last reported, or null if it has never been
  // negotiated with. Null is a reason to answer UNSUPPORTED, not a reason to
  // assume defaults.
  const ProtocolSupportEnvelope* GetSupport(const FrameId& frame_id) const;

  // Called when a frame's epoch is retired.
  void ForgetFrame(const FrameId& frame_id);
  void ForgetAll();

  // Builds an envelope carrying nothing but a code. Exposed so a caller that
  // refuses before the call is made produces the same shape.
  static ProtocolSupportEnvelope BareEnvelope(const RequestId& request_id,
                                              const TabId& tab_id,
                                              ObservationResultCode code);

 private:
  struct Pending {
    RequestId request_id;
    TabId tab_id;
    FrameId frame_id;
    scoped_refptr<RendererCallDeadline> deadline;
  };

  void OnProtocolInfo(RequestId request_id, mojom::ProtocolInfoPtr info);
  void Settle(const RequestId& request_id, ProtocolSupportEnvelope envelope);

  CompletionCallback on_settled_;
  std::map<RequestId, Pending> pending_;
  std::map<FrameId, ProtocolSupportEnvelope> support_;

  base::WeakPtrFactory<ProtocolNegotiator> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_PROTOCOL_NEGOTIATOR_H_

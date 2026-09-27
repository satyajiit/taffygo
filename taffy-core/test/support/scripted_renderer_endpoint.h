// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_SCRIPTED_RENDERER_ENDPOINT_H_
#define TAFFY_TEST_SUPPORT_SCRIPTED_RENDERER_ENDPOINT_H_

#include <memory>
#include <string>
#include <vector>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/test/support/renderer_reply_plan.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/scoped_interface_endpoint_handle.h"

namespace content {
class RenderFrameHost;
}  // namespace content

// A renderer endpoint under the test's control, bound in place of the real one.
//
// The adversarial suite needs a renderer that answers badly on purpose. The
// real endpoint cannot be made to: it is the code being defended, and asking it
// to lie would mean shipping a way to make it lie. So the suite binds this
// instead, through the same associated interface the broker would have used,
// and drives it from a RendererReplyPlan.
//
// What that buys is the ability to prove the browser half's refusals against a
// renderer that is actively hostile rather than merely absent — which is the
// threat model's adversary A2 and the only honest way to test a trust boundary.
//
// What it does not buy, and must not be read as buying: the two attacks a
// well-behaved sender cannot mount. An unknown enumeration member and a
// malformed body are rejected by Mojo's own validator before any Taffy code
// runs, so they are proved by the compatibility fixtures and the parser
// fuzzers, not here. RendererReplyPlan says so at the top, and the suites cite
// it rather than quietly claiming coverage.
//
// Installing one replaces the real endpoint for that frame for the rest of the
// document's life. A test that wants both — a real endpoint on one frame and a
// scripted one on another — installs per frame, which is why InstallFor takes a
// RenderFrameHost and not a WebContents.
//
// UI thread only.

namespace taffy::test {

class ScriptedRendererEndpoint : public mojom::PageIntelligence {
 public:
  // Overrides the associated-interface binder for `host`'s current document, so
  // the next bind from the broker reaches this object. Must be called before
  // the broker binds, which in practice means before the first observation.
  static std::unique_ptr<ScriptedRendererEndpoint> InstallFor(
      content::RenderFrameHost* host);

  ScriptedRendererEndpoint(const ScriptedRendererEndpoint&) = delete;
  ScriptedRendererEndpoint& operator=(const ScriptedRendererEndpoint&) = delete;
  ~ScriptedRendererEndpoint() override;

  void SetPlan(RendererReplyPlan plan);
  const RendererReplyPlan& plan() const { return plan_; }

  // What the browser asked for, in order. A suite asserts on this when the
  // property is that the browser narrowed a request before it left the process:
  // the clamped budget arrives here, so this is where "a request can never
  // widen the granted policy" becomes observable.
  const std::vector<mojom::SnapshotRequestPtr>& received_snapshot_requests()
      const {
    return received_snapshot_requests_;
  }
  const std::vector<mojom::RendererActionCommandPtr>& received_commands()
      const {
    return received_commands_;
  }
  const std::vector<mojom::MediaTargetRequestPtr>&
  received_media_target_requests() const {
    return received_media_target_requests_;
  }
  const std::vector<std::string>& received_cancellations() const {
    return received_cancellations_;
  }

  // Emits the deltas the plan describes on the open subscription, if any.
  // Called by the test when it wants the stream to move, so that delta timing
  // is the test's decision rather than a race.
  void PumpDeltas();

  // mojom::PageIntelligence:
  void GetProtocolInfo(GetProtocolInfoCallback callback) override;
  void GetSnapshot(mojom::SnapshotRequestPtr request,
                   GetSnapshotCallback callback) override;
  void Subscribe(mojom::PageSubscriptionOptionsPtr options,
                 mojo::PendingRemote<mojom::PageDeltaClient> client,
                 SubscribeCallback callback) override;
  void ResolveNode(mojom::ResolveNodeRequestPtr request,
                   ResolveNodeCallback callback) override;
  void InspectMediaTarget(mojom::MediaTargetRequestPtr request,
                          InspectMediaTargetCallback callback) override;
  void ExecuteRendererAction(mojom::RendererActionCommandPtr command,
                             ExecuteRendererActionCallback callback) override;
  void Cancel(const std::string& command_id) override;

 private:
  ScriptedRendererEndpoint();

  void Bind(mojo::ScopedInterfaceEndpointHandle handle);

  mojom::PageSnapshotPtr BuildSnapshot(const mojom::SnapshotRequest& request);

  RendererReplyPlan plan_;

  std::vector<mojom::SnapshotRequestPtr> received_snapshot_requests_;
  std::vector<mojom::RendererActionCommandPtr> received_commands_;
  std::vector<mojom::MediaTargetRequestPtr> received_media_target_requests_;
  std::vector<std::string> received_cancellations_;

  // The identity the browser told this endpoint it was, learned exactly the
  // way the production endpoint learns it: the tab from the snapshot request
  // (renderer/page_intelligence_endpoint.cc keeps it in tab_id_ for the same
  // reason), the frame from the subscribe options. A delta echoes both back,
  // and the browser drops one that names anything else - so a fixture that
  // invented them would be testing the identity check rather than whatever the
  // test meant to test.
  std::string tab_id_;
  std::string subscription_frame_id_;

  // The open subscription, when there is one.
  mojo::Remote<mojom::PageDeltaClient> delta_client_;
  std::string subscription_id_;
  std::string subscription_epoch_;
  uint64_t subscription_revision_ = 0;
  uint64_t next_event_sequence_ = 1;

  // Reply callbacks for the "never answers" behaviours.
  //
  // Parked rather than dropped, and the difference is the whole of why these
  // members exist. Returning from a mojo method without running its reply
  // callback destroys the callback, and Mojo treats that as a bug in the
  // implementation: with dcheck_always_on it aborts the process with
  // "callback was destroyed without first either being run or its
  // corresponding binding being closed". The suite's intent - a renderer that
  // answers nothing, so that the browser's own deadline is what settles the
  // request - is unchanged by holding the callback instead, because a held
  // callback is still never run.
  //
  // Declared before receiver_ so they outlive it. Members are destroyed in
  // reverse declaration order, so the receiver closes its binding first and
  // these are then destroyed against a closed binding, which is the case Mojo
  // explicitly allows.
  std::vector<GetProtocolInfoCallback> parked_protocol_info_callbacks_;
  std::vector<GetSnapshotCallback> parked_snapshot_callbacks_;
  std::vector<SubscribeCallback> parked_subscribe_callbacks_;
  std::vector<ResolveNodeCallback> parked_resolve_node_callbacks_;
  std::vector<InspectMediaTargetCallback> parked_media_target_callbacks_;
  std::vector<ExecuteRendererActionCallback> parked_action_callbacks_;

  mojo::AssociatedReceiver<mojom::PageIntelligence> receiver_{this};
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_SCRIPTED_RENDERER_ENDPOINT_H_

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_TEST_ENDPOINT_TEST_HARNESS_H_
#define TAFFY_RENDERER_TEST_ENDPOINT_TEST_HARNESS_H_

#include <memory>
#include <string>
#include <vector>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/renderer/page_intelligence_endpoint.h"
#include "content/public/test/render_view_test.h"

namespace taffy::test {

// A RenderViewTest with one PageIntelligenceEndpoint bound to the main frame,
// plus the request shapes every test in this directory needs.
//
// These tests need a real renderer: an accessibility tree, layout geometry, a
// selection, and a document that Blink actually parsed. The properties that
// do NOT need one - node identity, redaction classification, capability
// honesty, action decoding, delta drop order - are unit tests one directory
// up, and they are the ones that must never be skipped. What is here is
// everything that can only be proved against a document.
class EndpointTestHarness : public content::RenderViewTest {
 public:
  EndpointTestHarness();
  EndpointTestHarness(const EndpointTestHarness&) = delete;
  EndpointTestHarness& operator=(const EndpointTestHarness&) = delete;
  ~EndpointTestHarness() override;

  // content::RenderViewTest:
  void SetUp() override;
  void TearDown() override;

  // Loads markup and binds a fresh endpoint to the resulting document. Every
  // call is a new document and therefore a new epoch, which is the property
  // the epoch tests depend on.
  void LoadAndBind(const std::string& html);

  PageIntelligenceEndpoint* endpoint() { return endpoint_.get(); }
  const std::string& page_epoch() const { return page_epoch_; }

  // A document-scoped request asking for every adapter as optional. Optional
  // rather than required on purpose: a fixture with no forms must not make
  // the whole snapshot UNSUPPORTED, and the per-adapter reports are what say
  // which ones had anything to contribute.
  mojom::SnapshotRequestPtr DocumentRequest() const;
  mojom::SnapshotRequestPtr RequestWithScope(
      mojom::ObservationScope scope) const;

  // Runs one snapshot request to completion and returns the reply.
  mojom::SnapshotResultPtr Snapshot(mojom::SnapshotRequestPtr request);

  // Hands one snapshot request to the endpoint and returns at once, with `out`
  // left for whenever the endpoint answers.
  //
  // The blocking form above cannot express the property this exists for: a
  // read of a document that has not arrived is held rather than answered, and
  // "it has not answered yet" is exactly what a run loop hides.
  void SubmitSnapshot(mojom::SnapshotRequestPtr request,
                      mojom::SnapshotResultPtr* out);

  // A re-resolution request for one node id, stated against `epoch`. The
  // epoch is a parameter rather than this harness's current one because the
  // only case worth writing a test about is the one where the two disagree.
  mojom::ResolveNodeRequestPtr ResolveRequest(const std::string& node_id,
                                              const std::string& epoch) const;

  // Runs one node re-resolution to completion and returns the reply.
  mojom::ResolveNodeResultPtr Resolve(mojom::ResolveNodeRequestPtr request);

  // Revalidates one exact media node through the renderer-only inspection
  // seam. No compositor pixels are involved in this harness; this is the
  // point where the renderer must refuse a challenge before the browser can
  // ask its compositor for any.
  mojom::MediaTargetResultPtr InspectMedia(const std::string& node_id,
                                           uint64_t required_graph_revision,
                                           mojom::MediaTargetKind kind);

  // Runs one already-narrowed renderer action to completion. This bypasses no
  // renderer gate: the endpoint still translates the command, rechecks the
  // live node and uses the accessibility executor. It only replaces Mojo
  // transport with a direct call in the render-view test.
  mojom::RendererActionResultPtr Execute(
      mojom::RendererActionCommandPtr command);

  // Every string a snapshot carries that could hold page content: node names,
  // descriptions, text runs, normalized values, attribute values, source
  // locators, warning detail codes, and destination serializations. The leak
  // assertions search all of them, because a secret that escaped through a
  // source locator is still a secret that escaped.
  static std::vector<std::string> AllStringsIn(
      const mojom::PageSnapshot& snapshot);
  static std::vector<std::string> AllStringsIn(
      const mojom::SnapshotResult& result);

  // The adapter report for one kind, or null when the snapshot carries none.
  static const mojom::AdapterReport* ReportFor(
      const mojom::PageSnapshot& snapshot,
      mojom::AdapterKind kind);

  // Nodes of one role.
  static std::vector<const mojom::SemanticNode*> NodesWithRole(
      const mojom::PageSnapshot& snapshot,
      mojom::SemanticRole role);

  static bool HasState(const mojom::SemanticNode& node,
                       mojom::NodeState state);

 private:
  std::unique_ptr<PageIntelligenceEndpoint> endpoint_;
  std::string page_epoch_;
  int epoch_ordinal_ = 0;
};

}  // namespace taffy::test

#endif  // TAFFY_RENDERER_TEST_ENDPOINT_TEST_HARNESS_H_

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_TAFFY_CONTENT_RENDERER_CLIENT_H_
#define TAFFY_TEST_TAFFY_CONTENT_RENDERER_CLIENT_H_

#include "content/shell/renderer/shell_content_renderer_client.h"

namespace content {
class RenderFrame;
}  // namespace content

// content_shell's renderer client, plus the one thing the product's renderer
// client does that a test binary otherwise does not: create a
// TaffyRenderFrameObserver for every frame.
//
// WHY THIS EXISTS. Whether a Taffy renderer endpoint is bound is a property of
// the BINARY, not of a test. The browser half answers UNSUPPORTED when nothing
// answers negotiation, and then answers UNSUPPORTED to every observation after
// it — so a correctness test that only asserted "not an error" would pass while
// measuring an empty room. RendererEndpointRequirement exists to turn that into
// a loud failure, and until 2026-08-19 it fired on every test in
// //taffy/test/correctness, because taffy_browsertests was a plain
// content_shell browser-test binary with content_shell's own renderer client in
// it. This class is the missing half.
//
// It is the test binary's counterpart to what chromium/patches/0007 specifies
// for the product: ChromeContentRendererClient::RenderFrameCreated constructing
// the same observer. Two call sites, one class, and the observer itself is the
// shipping one rather than a fake — which is the point, because these suites
// are about what the real endpoint produces.
//
// It costs no upstream patch: ShellContentRendererClient is public, its
// RenderFrameCreated is virtual, and ShellMainDelegate lets a subclass return a
// different client.
//
// WHAT WAS MISSING, and it was not this class. From 2026-08-19 this file
// recorded that the delegate was installed and the observer constructed while
// negotiation still reported UNSUPPORTED, with the cause unfound. It was found
// on 2026-09-07 and it was not on the binding path at all:
// //taffy/renderer/page_intelligence_endpoint.cc restated the protocol version
// in a private constant and that copy had gone two minor steps stale, so
// ProtocolNegotiator::OnProtocolInfo refused every reply as a version it does
// not speak. The refusal was correct; the number was wrong. It was wrong for
// the product too, not only for this binary.
//
// Both of the suspects this comment used to list were wrong, and the evidence
// against them was already available. `is_actionable_` initialises to true and
// only Retire() clears it, so remote() does bind; and the renderer was
// observed serving GetSnapshot in a crash on 2026-09-07, which an unbound
// endpoint could not have done. Recorded because the ranking was confident and
// misleading: it sent a reader to the transport when the disagreement was in
// the payload.
//
// The requirement was never the thing to relax — an unbound endpoint and a
// broken one still produce the same result code, and that check is still the
// only thing that tells them apart. It is what made a stale literal visible at
// all, by refusing to let thirty-one benchmark tests pass while measuring an
// empty room.
//
// //taffy/contracts/bip/codegen/layout.py now names that restatement in
// VERSION_RESTATEMENTS and generate.py --check fails when it disagrees with
// bip.version.json, so the same drift cannot recur silently.
namespace taffy::test {

class TaffyContentRendererClient : public content::ShellContentRendererClient {
 public:
  explicit TaffyContentRendererClient(bool is_browsertest);
  TaffyContentRendererClient(const TaffyContentRendererClient&) = delete;
  TaffyContentRendererClient& operator=(const TaffyContentRendererClient&) =
      delete;
  ~TaffyContentRendererClient() override;

  // content::ContentRendererClient:
  void RenderFrameCreated(content::RenderFrame* render_frame) override;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_TAFFY_CONTENT_RENDERER_CLIENT_H_

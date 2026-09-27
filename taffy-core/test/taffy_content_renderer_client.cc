// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/taffy_content_renderer_client.h"

#include "taffy/components/filtering/renderer/cosmetic_filter_render_frame_observer.h"
#include "taffy/renderer/taffy_render_frame_observer.h"

namespace taffy::test {

TaffyContentRendererClient::TaffyContentRendererClient(bool is_browsertest)
    : content::ShellContentRendererClient(is_browsertest) {}

TaffyContentRendererClient::~TaffyContentRendererClient() = default;

void TaffyContentRendererClient::RenderFrameCreated(
    content::RenderFrame* render_frame) {
  content::ShellContentRendererClient::RenderFrameCreated(render_frame);

  // Owned by the RenderFrame: TaffyRenderFrameObserver deletes itself in
  // OnDestruct(), which is content::RenderFrameObserver's lifetime contract.
  // Constructing it is the whole of what this class adds, and it is also
  // exactly what chromium/patches/0007 specifies for the product's renderer
  // client — deliberately the same one-line shape, so that the suites test the
  // observer the product ships rather than a test-only stand-in.
  new TaffyRenderFrameObserver(render_frame);
  new taffy::filtering::CosmeticFilterRenderFrameObserver(render_frame);
}

}  // namespace taffy::test

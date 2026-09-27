// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/renderer/cosmetic_filter_render_frame_observer.h"

#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace taffy::filtering {

CosmeticFilterRenderFrameObserver::CosmeticFilterRenderFrameObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {
  BindAndStart();
}

CosmeticFilterRenderFrameObserver::~CosmeticFilterRenderFrameObserver() =
    default;

void CosmeticFilterRenderFrameObserver::OnDestruct() {
  delete this;
}

void CosmeticFilterRenderFrameObserver::DidCreateNewDocument() {
  BindAndStart();
}

void CosmeticFilterRenderFrameObserver::DidCommitProvisionalLoad(
    ui::PageTransition) {
  BindAndStart();
}

void CosmeticFilterRenderFrameObserver::BindAndStart() {
  agent_.reset();
  host_.reset();
  if (!render_frame() || !render_frame()->GetWebFrame()) {
    return;
  }
  render_frame()->GetRemoteAssociatedInterfaces()->GetInterface(&host_);
  agent_ = std::make_unique<CosmeticFilterAgent>(render_frame()->GetWebFrame(),
                                                 host_);
  agent_->Start();
}

}  // namespace taffy::filtering

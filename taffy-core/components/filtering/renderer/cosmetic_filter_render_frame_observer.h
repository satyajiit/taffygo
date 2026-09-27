// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_RENDERER_COSMETIC_FILTER_RENDER_FRAME_OBSERVER_H_
#define TAFFY_COMPONENTS_FILTERING_RENDERER_COSMETIC_FILTER_RENDER_FRAME_OBSERVER_H_

#include <memory>

#include "taffy/components/filtering/mojom/cosmetic_filter.mojom.h"
#include "taffy/components/filtering/renderer/cosmetic_filter_agent.h"
#include "content/public/renderer/render_frame_observer.h"
#include "mojo/public/cpp/bindings/associated_remote.h"

namespace content {
class RenderFrame;
}  // namespace content

namespace taffy::filtering {

// Per-frame cosmetics. Self-deletes in OnDestruct(). Does not hang off
// TaffyRenderFrameObserver and does not reuse DomMutationSignalSource.
class CosmeticFilterRenderFrameObserver : public content::RenderFrameObserver {
 public:
  explicit CosmeticFilterRenderFrameObserver(
      content::RenderFrame* render_frame);
  CosmeticFilterRenderFrameObserver(const CosmeticFilterRenderFrameObserver&) =
      delete;
  CosmeticFilterRenderFrameObserver& operator=(
      const CosmeticFilterRenderFrameObserver&) = delete;
  ~CosmeticFilterRenderFrameObserver() override;

  void OnDestruct() override;
  void DidCreateNewDocument() override;
  void DidCommitProvisionalLoad(ui::PageTransition transition) override;

 private:
  void BindAndStart();

  mojo::AssociatedRemote<mojom::CosmeticFilterHost> host_;
  std::unique_ptr<CosmeticFilterAgent> agent_;
};

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_RENDERER_COSMETIC_FILTER_RENDER_FRAME_OBSERVER_H_

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_TAFFY_RENDER_FRAME_OBSERVER_H_
#define TAFFY_RENDERER_TAFFY_RENDER_FRAME_OBSERVER_H_

#include <memory>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/renderer/page_intelligence_endpoint.h"
#include "content/public/renderer/render_frame_observer.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"

namespace blink {
class WebElement;
}  // namespace blink

namespace content {
class RenderFrame;
}  // namespace content

namespace taffy {

// Owns one PageIntelligenceEndpoint per render frame and wires it to Blink's
// lifecycle.
//
// This class is where the epoch boundary is enforced on the renderer side.
// The rule is simple and absolute: a new document gets a new endpoint, and
// the old endpoint is invalidated before the new one exists. Node ids, the
// store, the coalescer, and every subscription die with it. That is why no
// handle from a previous document can resolve in the next one - not because
// something compares epochs on the way in, but because the object that could
// have answered is gone.
//
// The observer deletes itself in OnDestruct(), which is the RenderFrame
// lifetime contract.
class TaffyRenderFrameObserver : public content::RenderFrameObserver {
 public:
  explicit TaffyRenderFrameObserver(content::RenderFrame* render_frame);
  TaffyRenderFrameObserver(const TaffyRenderFrameObserver&) = delete;
  TaffyRenderFrameObserver& operator=(const TaffyRenderFrameObserver&) = delete;
  ~TaffyRenderFrameObserver() override;

  // content::RenderFrameObserver:
  void OnDestruct() override;
  void DidCreateNewDocument() override;
  void DidCommitProvisionalLoad(ui::PageTransition transition) override;
  void DidDispatchDOMContentLoadedEvent() override;
  void DidChangeScrollOffset() override;
  void FocusedElementChanged(const blink::WebElement& element) override;
  void DidObserveLayoutShift(double score, bool after_input_or_scroll) override;

 private:
  void BindEndpoint(
      mojo::PendingAssociatedReceiver<mojom::PageIntelligence> receiver);

  // Retires the current endpoint, if any. Called before a new document
  // exists, never after.
  void RetireEndpoint(mojom::InvalidationReason reason);

  std::unique_ptr<PageIntelligenceEndpoint> endpoint_;

  // Renderer-local frame identity. It is NOT the broker's FrameId: the broker
  // owns that, and a renderer that could choose one could claim to be another
  // frame. It exists only so the store has a stable value inside this
  // process, and it is never sent anywhere. The broker overwrites frame
  // identity on every message it receives.
  //
  // That last sentence was an intention for as long as it stood here, and
  // nothing performed the overwrite: this value was stamped on every semantic
  // node and written straight through to the isolated core, which refuses a
  // payload unless every node row names the frame the envelope names — a
  // browser-space value it could never equal. So no real observation ever
  // decoded. The overwrite now happens where the bytes are framed, in
  // EncodeGraphPayload and EncodeDeltaPayload, which take the browser's
  // FrameId and write it on every row (decision 0051).
  const FrameId local_frame_id_;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_TAFFY_RENDER_FRAME_OBSERVER_H_

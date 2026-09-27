// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/page_media_observation.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/notreached.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/page_media_result.h"
#include "taffy/browser/page_pdf_observation.h"
#include "taffy/browser/page_visual_capture.h"
#include "taffy/browser/page_video_caption_facts.h"
#include "taffy/browser/profile_page_media_store.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/renderer_text_rescan.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace taffy {
namespace {

namespace bip_mojom = mojom;
namespace core_mojom = core_service::mojom;

constexpr int kMaximumCaptureDimension = 960;
constexpr base::TimeDelta kMaximumObservationLifetime = base::Seconds(10);
constexpr base::TimeDelta kSurfaceCopyTimeout = base::Seconds(2);

bool BoundedIdentifier(std::string_view value) {
  return !value.empty() && value.size() <= core_mojom::kMaxIdentifierBytes;
}

class PageMediaObservationRun
    : public base::RefCounted<PageMediaObservationRun> {
 public:
  PageMediaObservationRun(content::BrowserContext* browser_context,
                          PageMediaObservationRequest request,
                          scoped_refptr<ProfilePageMediaStore> media_store,
                          PageMediaObservationCompletion completion)
      : browser_context_(browser_context),
        request_(std::move(request)),
        media_store_(std::move(media_store)),
        completion_(std::move(completion)) {}

  void Start() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!IsValidRequest()) {
      Finish(nullptr);
      return;
    }
    deadline_ = base::TimeTicks::Now() +
                std::min(request_.deadline, kMaximumObservationLifetime);
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&PageMediaObservationRun::OnDeadline,
                       weak_factory_.GetWeakPtr()),
        deadline_ - base::TimeTicks::Now());
    InspectBeforeCapture();
  }

  void Cancel() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    Finish(nullptr);
  }

 private:
  friend class base::RefCounted<PageMediaObservationRun>;
  ~PageMediaObservationRun() = default;

  bool IsValidRequest() const {
    return browser_context_ && media_store_ && completion_ &&
           BoundedIdentifier(request_.task_id) &&
           BoundedIdentifier(request_.effect_id) &&
           request_.service_generation != 0u &&
           BoundedIdentifier(request_.tab_id) &&
           BoundedIdentifier(request_.frame_id) &&
           BoundedIdentifier(request_.page_epoch) &&
           request_.minimum_graph_revision != 0u &&
           request_.deadline.is_positive() &&
           request_.kind != PageMediaObservationKind::kPdf &&
           ((request_.kind == PageMediaObservationKind::kPageScreenshot &&
             !request_.node_id) ||
            (request_.kind != PageMediaObservationKind::kPageScreenshot &&
             request_.node_id)) &&
           (!request_.node_id || BoundedIdentifier(*request_.node_id));
  }

  content::WebContents* ResolveLiveDocument() const {
    TaffyPageIntelligenceHost* host =
        FindPageIntelligenceHost(browser_context_, request_.tab_id);
    if (!host) {
      return nullptr;
    }
    content::WebContents* web_contents = host->observed_web_contents();
    content::RenderFrameHost* frame =
        web_contents ? web_contents->GetPrimaryMainFrame() : nullptr;
    FrameObservationEndpoint* endpoint =
        frame ? FrameObservationEndpoint::GetForCurrentDocument(frame)
              : nullptr;
    if (!frame || !frame->IsActive() || !frame->IsRenderFrameLive() ||
        !endpoint || !endpoint->is_actionable() ||
        endpoint->frame_id().value != request_.frame_id ||
        endpoint->page_epoch().value != request_.page_epoch ||
        endpoint->last_reported_revision() < request_.minimum_graph_revision) {
      return nullptr;
    }
    return web_contents;
  }

  FrameObservationEndpoint* ResolveLiveEndpoint() const {
    content::WebContents* web_contents = ResolveLiveDocument();
    return web_contents ? FrameObservationEndpoint::GetForCurrentDocument(
                              web_contents->GetPrimaryMainFrame())
                        : nullptr;
  }

  bip_mojom::MediaTargetKind TargetKind() const {
    switch (request_.kind) {
      case PageMediaObservationKind::kImage:
        return bip_mojom::MediaTargetKind::kImage;
      case PageMediaObservationKind::kVideo:
        return bip_mojom::MediaTargetKind::kVideo;
      case PageMediaObservationKind::kPageScreenshot:
        return bip_mojom::MediaTargetKind::kPageScreenshot;
      case PageMediaObservationKind::kPdf:
        break;
    }
    NOTREACHED();
  }

  void InspectBeforeCapture() {
    FrameObservationEndpoint* endpoint = ResolveLiveEndpoint();
    if (!endpoint || !endpoint->remote().is_bound()) {
      Finish(nullptr);
      return;
    }
    before_request_id_ =
        "media-before-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
    auto request = bip_mojom::MediaTargetRequest::New();
    request->request_id = before_request_id_;
    request->expected_page_epoch = request_.page_epoch;
    request->node_id = request_.node_id.value_or(std::string());
    request->required_graph_revision = request_.minimum_graph_revision;
    request->expected_kind = TargetKind();
    request->include_loaded_caption_cues = false;
    endpoint->remote()->InspectMediaTarget(
        std::move(request),
        base::BindOnce(&PageMediaObservationRun::OnInspectedBeforeCapture,
                       weak_factory_.GetWeakPtr()));
  }

  bool ValidateGeometry(const bip_mojom::MediaTargetResult& result,
                        std::string_view expected_request_id) const {
    return result.request_id == expected_request_id &&
           result.code == bip_mojom::MediaTargetResultCode::kOk &&
           result.kind == TargetKind() && result.bounds &&
           result.observed_graph_revision >= request_.minimum_graph_revision &&
           result.bounds->width > 0 && result.bounds->height > 0 &&
           MediaTargetRedactionsAreValid(
               result,
               request_.kind == PageMediaObservationKind::kPageScreenshot);
  }

  void OnInspectedBeforeCapture(bip_mojom::MediaTargetResultPtr result) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    content::WebContents* web_contents = ResolveLiveDocument();
    content::RenderWidgetHostView* view =
        web_contents ? web_contents->GetRenderWidgetHostView() : nullptr;
    if (!result || !ValidateGeometry(*result, before_request_id_) || !view) {
      Finish(nullptr);
      return;
    }
    original_bounds_ = gfx::Rect(result->bounds->x, result->bounds->y,
                                 result->bounds->width, result->bounds->height);
    original_graph_revision_ = result->observed_graph_revision;
    original_redaction_bounds_ = MediaTargetRedactionBounds(*result);
    viewport_size_ = view->GetVisibleViewportSize();
    if (request_.kind == PageMediaObservationKind::kPageScreenshot &&
        original_bounds_ != gfx::Rect(viewport_size_)) {
      Finish(nullptr);
      return;
    }
    const std::optional<PageVisualCapturePlan> plan =
        BuildPageVisualCapturePlan(viewport_size_, original_bounds_,
                                   original_redaction_bounds_,
                                   kMaximumCaptureDimension);
    if (!plan ||
        (request_.kind != PageMediaObservationKind::kPageScreenshot &&
         !plan->output_redactions.empty())) {
      Finish(nullptr);
      return;
    }
    capture_bounds_ = plan->capture_bounds;
    output_size_ = plan->output_size;
    output_redaction_bounds_ = plan->output_redactions;
    output_scale_ppm_ = plan->output_scale_ppm;
    scoped_refptr<base::SequencedTaskRunner> ui =
        base::SequencedTaskRunner::GetCurrentDefault();
    view->CopyFromSurface(
        capture_bounds_, output_size_, kSurfaceCopyTimeout,
        base::BindOnce(&PageMediaObservationRun::CopyFinished, std::move(ui),
                       weak_factory_.GetWeakPtr()));
  }

  static void CopyFinished(scoped_refptr<base::SequencedTaskRunner> ui,
                           base::WeakPtr<PageMediaObservationRun> run,
                           const content::CopyFromSurfaceResult& result) {
    SkBitmap bitmap;
    if (result.has_value() && !result.value().bitmap.drawsNothing()) {
      bitmap = result.value().bitmap;
    }
    ui->PostTask(FROM_HERE,
                 base::BindOnce(&PageMediaObservationRun::OnSurfaceBitmap,
                                std::move(run), std::move(bitmap)));
  }

  void OnSurfaceBitmap(SkBitmap bitmap) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (finished_ || bitmap.drawsNothing() ||
        base::TimeTicks::Now() >= deadline_) {
      Finish(nullptr);
      return;
    }
    captured_at_monotonic_ms_ = static_cast<uint64_t>(
        base::TimeTicks::Now().since_origin().InMilliseconds());
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&EncodePageVisualPng, std::move(bitmap),
                       output_redaction_bounds_),
        base::BindOnce(&PageMediaObservationRun::OnPngEncoded,
                       weak_factory_.GetWeakPtr()));
  }

  void OnPngEncoded(std::optional<std::vector<uint8_t>> png) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    FrameObservationEndpoint* endpoint = ResolveLiveEndpoint();
    if (!png || png->empty() ||
        png->size() > core_mojom::kMaxMediaAttachmentBytes || !endpoint ||
        !endpoint->remote().is_bound()) {
      Finish(nullptr);
      return;
    }
    png_ = std::move(*png);
    after_request_id_ =
        "media-after-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
    auto request = bip_mojom::MediaTargetRequest::New();
    request->request_id = after_request_id_;
    request->expected_page_epoch = request_.page_epoch;
    request->node_id = request_.node_id.value_or(std::string());
    request->required_graph_revision = request_.minimum_graph_revision;
    request->expected_kind = TargetKind();
    request->include_loaded_caption_cues =
        request_.kind == PageMediaObservationKind::kVideo;
    endpoint->remote()->InspectMediaTarget(
        std::move(request),
        base::BindOnce(&PageMediaObservationRun::OnInspectedAfterCapture,
                       weak_factory_.GetWeakPtr()));
  }

  void OnInspectedAfterCapture(bip_mojom::MediaTargetResultPtr result) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!result || !ValidateGeometry(*result, after_request_id_) ||
        original_bounds_ != gfx::Rect(result->bounds->x, result->bounds->y,
                                      result->bounds->width,
                                      result->bounds->height) ||
        (request_.kind == PageMediaObservationKind::kPageScreenshot &&
         (result->observed_graph_revision != original_graph_revision_ ||
          MediaTargetRedactionBounds(*result) !=
              original_redaction_bounds_)) ||
        !ResolveLiveDocument()) {
      Finish(nullptr);
      return;
    }
    auto media = core_mojom::MediaObservationResult::New();
    media->kind = RenderedMediaKind(request_.kind);
    if ((request_.kind == PageMediaObservationKind::kImage &&
         !result->loaded_caption_cues.empty()) ||
        (request_.kind == PageMediaObservationKind::kVideo &&
         !PopulateVideoCaptionFacts(*result, *media, &rescan_))) {
      Finish(nullptr);
      return;
    }
    auto stored = media_store_->StoreRenderedPng(
        ProfilePageMediaStore::StoreRequest{
            .task_id = request_.task_id,
            .observation_effect_id = request_.effect_id,
            .service_generation = request_.service_generation,
            .tab_id = request_.tab_id,
            .frame_id = request_.frame_id,
            .page_epoch = request_.page_epoch,
            .graph_revision = result->observed_graph_revision,
            .node_id = request_.node_id,
            .width_px = static_cast<uint32_t>(output_size_.width()),
            .height_px = static_cast<uint32_t>(output_size_.height()),
        },
        std::move(png_), base::TimeTicks::Now());
    if (!stored) {
      Finish(nullptr);
      return;
    }
    media->attachment_handle = std::move(stored->handle);
    media->attachment_mime_type = std::move(stored->mime_type);
    media->width_px = stored->width_px;
    media->height_px = stored->height_px;
    if (request_.kind == PageMediaObservationKind::kPageScreenshot) {
      media->capture_provenance = MakeCaptureProvenance(
          capture_bounds_, viewport_size_, output_scale_ppm_,
          captured_at_monotonic_ms_,
          static_cast<uint32_t>(output_redaction_bounds_.size()));
    }
    Finish(std::move(media));
  }

  void OnDeadline() { Finish(nullptr); }

  void Finish(core_mojom::MediaObservationResultPtr result) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (finished_) {
      return;
    }
    // Completion removes the broker's cancellation closure, which may own
    // this run's last external reference. Keep the object alive until this
    // member has returned even when completion settles synchronously.
    scoped_refptr<PageMediaObservationRun> keep_alive(this);
    finished_ = true;
    weak_factory_.InvalidateWeakPtrs();
    if (completion_) {
      std::move(completion_)
          .Run(std::move(result), rescan_.redacted_span_count);
    }
  }

  raw_ptr<content::BrowserContext> browser_context_;
  PageMediaObservationRequest request_;
  scoped_refptr<ProfilePageMediaStore> media_store_;
  PageMediaObservationCompletion completion_;
  base::TimeTicks deadline_;
  bool finished_ = false;

  std::string before_request_id_;
  std::string after_request_id_;
  uint64_t original_graph_revision_ = 0u;
  gfx::Rect original_bounds_;
  std::vector<gfx::Rect> original_redaction_bounds_;
  gfx::Rect capture_bounds_;
  gfx::Size viewport_size_;
  gfx::Size output_size_;
  std::vector<gfx::Rect> output_redaction_bounds_;
  uint32_t output_scale_ppm_ = 0u;
  uint64_t captured_at_monotonic_ms_ = 0u;
  std::vector<uint8_t> png_;
  RescanTally rescan_;

  base::WeakPtrFactory<PageMediaObservationRun> weak_factory_{this};
};

}  // namespace

base::OnceClosure StartPageMediaObservation(
    content::BrowserContext* browser_context,
    PageMediaObservationRequest request,
    scoped_refptr<ProfilePageMediaStore> media_store,
    PageMediaObservationCompletion completion) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (request.kind == PageMediaObservationKind::kPdf) {
    return StartPagePdfObservation(browser_context, std::move(request),
                                   std::move(completion));
  }
  auto run = base::MakeRefCounted<PageMediaObservationRun>(
      browser_context, std::move(request), std::move(media_store),
      std::move(completion));
  run->Start();
  return base::BindOnce(&PageMediaObservationRun::Cancel, std::move(run));
}

}  // namespace taffy

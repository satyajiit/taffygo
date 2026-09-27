// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/field_challenge_capture.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace taffy {
namespace {

using NodeFactsCallback =
    base::OnceCallback<void(std::optional<ResolvedNodeFacts>)>;

// Small enough for Android's browser-to-UI Mojo message even after its other
// fields are encoded. Random-noise pixels compress poorly, so both dimensions
// and bytes are bounded independently.
constexpr int kMaximumChallengeDimension = 384;
constexpr size_t kMaximumChallengePngBytes = 512u * 1024u;
constexpr base::TimeDelta kMaximumCaptureLifetime = base::Seconds(4);
constexpr base::TimeDelta kSurfaceCopyTimeout = base::Seconds(2);

bool ValidBounds(const std::optional<ChallengeBounds>& bounds) {
  return bounds.has_value() && bounds->width > 0 && bounds->height > 0;
}

bool SameBounds(const std::optional<ChallengeBounds>& left,
                const std::optional<ChallengeBounds>& right) {
  if (left.has_value() != right.has_value()) {
    return false;
  }
  return !left.has_value() ||
         (left->x == right->x && left->y == right->y &&
          left->width == right->width && left->height == right->height);
}

bool SameChallengeTarget(const ResolvedNodeFacts& before,
                         const ResolvedNodeFacts& after) {
  return before.node_id == after.node_id &&
         after.observed_at_revision >= before.observed_at_revision &&
         before.challenge_kind == after.challenge_kind &&
         before.sensitivity == after.sensitivity &&
         SameBounds(before.challenge_bounds, after.challenge_bounds);
}

std::optional<std::vector<uint8_t>> EncodePng(SkBitmap bitmap) {
  if (bitmap.drawsNothing()) {
    return std::nullopt;
  }
  return gfx::PNGCodec::FastEncodeBGRASkBitmap(
      bitmap, /*discard_transparency=*/false);
}

class FieldChallengeCaptureRun
    : public base::RefCounted<FieldChallengeCaptureRun> {
 public:
  FieldChallengeCaptureRun(content::BrowserContext* browser_context,
                           std::string tab_id,
                           std::string node_id,
                           ResolvedNodeFacts expected,
                           FieldChallengePresentationCallback completion)
      : browser_context_(browser_context),
        tab_id_(std::move(tab_id)),
        node_id_(std::move(node_id)),
        expected_(std::move(expected)),
        completion_(std::move(completion)) {}

  void Start() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    // Each refusal names itself. Without this the coordinator's
    // `[taffy_field_request_abandoned] at=no-challenge-picture` was the whole
    // account of a sheet that never opened, and it says only that there was no
    // picture — not whether the classifier named no element, whether the
    // element it named has no visible box, or whether the surface copy failed.
    // A phone spent a run on the myAadhaar CAPTCHA that way (decision 0199).
    // Every clause below is a compiled-in name.
    if (!browser_context_ || tab_id_.empty() || node_id_.empty() ||
        expected_.node_id.value != node_id_ || !completion_) {
      Refuse("request-shape");
      return;
    }
    if (expected_.challenge_kind != ChallengeKind::kImage &&
        expected_.challenge_kind != ChallengeKind::kInteractive) {
      Refuse("not-a-drawable-challenge");
      return;
    }
    // The one that costs a sheet. The field is an image challenge and the
    // renderer sent no box for its picture, which means the classifier named
    // no element or the element it named had no visible box at resolve time.
    if (!ValidBounds(expected_.challenge_bounds)) {
      Refuse("no-challenge-bounds");
      return;
    }
    TaffyPageIntelligenceHost* host =
        FindPageIntelligenceHost(browser_context_, tab_id_);
    const std::optional<DirectObservationContext> live =
        host ? host->BuildDirectObservationContext() : std::nullopt;
    if (!live || live->tab_id != tab_id_ || live->frame_id.empty() ||
        live->page_epoch.empty() || live->origin.empty()) {
      Refuse("no-live-document");
      return;
    }
    frame_id_ = live->frame_id;
    page_epoch_ = live->page_epoch;
    origin_ = live->origin;
    deadline_ = base::TimeTicks::Now() + kMaximumCaptureLifetime;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&FieldChallengeCaptureRun::OnDeadline,
                       weak_factory_.GetWeakPtr()),
        kMaximumCaptureLifetime);
    ResolveAgain(base::BindOnce(&FieldChallengeCaptureRun::OnResolvedBefore,
                                weak_factory_.GetWeakPtr()));
  }

  void Cancel() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    Finish(std::nullopt, nullptr);
  }

 private:
  friend class base::RefCounted<FieldChallengeCaptureRun>;
  ~FieldChallengeCaptureRun() = default;

  content::WebContents* ResolveWebContents() const {
    TaffyPageIntelligenceHost* host =
        FindPageIntelligenceHost(browser_context_, tab_id_);
    if (!host) {
      return nullptr;
    }
    const std::optional<DirectObservationContext> live =
        host->BuildDirectObservationContext();
    return live && SameDocument(*live) ? host->observed_web_contents()
                                      : nullptr;
  }

  bool SameDocument(const DirectObservationContext& live) const {
    return live.tab_id == tab_id_ && live.frame_id == frame_id_ &&
           live.page_epoch == page_epoch_ && live.origin == origin_;
  }

  void ResolveAgain(NodeFactsCallback callback) {
    TaffyPageIntelligenceHost* host =
        FindPageIntelligenceHost(browser_context_, tab_id_);
    if (!host) {
      std::move(callback).Run(std::nullopt);
      return;
    }
    const std::optional<DirectObservationContext> live =
        host->BuildDirectObservationContext();
    if (!live || !SameDocument(*live)) {
      std::move(callback).Run(std::nullopt);
      return;
    }
    NodeHandle handle;
    handle.tab_id = TabId{live->tab_id};
    handle.frame_id = FrameId{live->frame_id};
    handle.page_epoch = PageEpoch{live->page_epoch};
    // The original floor makes a challenge-target mutation a refusal. An
    // unrelated graph advance may still resolve and is harmless.
    handle.graph_revision = expected_.observed_at_revision;
    handle.node_id = SemanticNodeId{node_id_};
    handle.expected_origin.kind = OriginKind::kTuple;
    handle.expected_origin.serialization = live->origin;
    if (!handle.is_well_formed()) {
      std::move(callback).Run(std::nullopt);
      return;
    }
    host->ResolveNodeFacts(handle, std::move(callback));
  }

  void OnResolvedBefore(std::optional<ResolvedNodeFacts> current) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    content::WebContents* web_contents = ResolveWebContents();
    content::RenderWidgetHostView* view =
        web_contents ? web_contents->GetRenderWidgetHostView() : nullptr;
    if (!current || !SameChallengeTarget(expected_, *current) || !view ||
        base::TimeTicks::Now() >= deadline_) {
      Refuse("target-moved");
      return;
    }
    before_ = std::move(*current);
    const gfx::Size viewport = view->GetVisibleViewportSize();
    if (viewport.IsEmpty()) {
      Refuse("no-viewport");
      return;
    }
    const ChallengeBounds& raw = *before_.challenge_bounds;
    capture_bounds_ = gfx::Rect(raw.x, raw.y, raw.width, raw.height);
    capture_bounds_.Intersect(gfx::Rect(viewport));
    if (capture_bounds_.IsEmpty()) {
      // The picture is off the part of the page the person is looking at.
      // The only clause a caller treats differently: nothing is wrong with the
      // target, so the sheet is one scroll away rather than impossible
      // (decision 0215).
      Refuse(kFieldChallengeRefusedOffScreen);
      return;
    }

    if (before_.challenge_kind == ChallengeKind::kInteractive) {
      FieldChallengePresentation presentation;
      presentation.highlight = NormalizedFieldHighlight{
          .left = static_cast<float>(capture_bounds_.x()) / viewport.width(),
          .top = static_cast<float>(capture_bounds_.y()) / viewport.height(),
          .right = static_cast<float>(capture_bounds_.right()) /
                   viewport.width(),
          .bottom = static_cast<float>(capture_bounds_.bottom()) /
                    viewport.height(),
      };
      highlight_ = presentation.highlight;
      ResolveAgain(base::BindOnce(&FieldChallengeCaptureRun::OnResolvedAfter,
                                  weak_factory_.GetWeakPtr()));
      return;
    }

    const double scale =
        std::min({1.0,
                  static_cast<double>(kMaximumChallengeDimension) /
                      capture_bounds_.width(),
                  static_cast<double>(kMaximumChallengeDimension) /
                      capture_bounds_.height()});
    output_size_ = gfx::Size(
        std::max(1, static_cast<int>(std::floor(capture_bounds_.width() *
                                                scale))),
        std::max(1, static_cast<int>(std::floor(capture_bounds_.height() *
                                                scale))));
    scoped_refptr<base::SequencedTaskRunner> ui =
        base::SequencedTaskRunner::GetCurrentDefault();
    view->CopyFromSurface(
        capture_bounds_, output_size_, kSurfaceCopyTimeout,
        base::BindOnce(&FieldChallengeCaptureRun::CopyFinished, std::move(ui),
                       weak_factory_.GetWeakPtr()));
  }

  static void CopyFinished(scoped_refptr<base::SequencedTaskRunner> ui,
                           base::WeakPtr<FieldChallengeCaptureRun> run,
                           const content::CopyFromSurfaceResult& result) {
    SkBitmap bitmap;
    if (result.has_value() && !result.value().bitmap.drawsNothing()) {
      bitmap = result.value().bitmap;
      bitmap.setImmutable();
    }
    ui->PostTask(FROM_HERE,
                 base::BindOnce(&FieldChallengeCaptureRun::OnSurfaceBitmap,
                                std::move(run), std::move(bitmap)));
  }

  void OnSurfaceBitmap(SkBitmap bitmap) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (bitmap.drawsNothing() || base::TimeTicks::Now() >= deadline_) {
      Refuse("no-surface-bitmap");
      return;
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&EncodePng, std::move(bitmap)),
        base::BindOnce(&FieldChallengeCaptureRun::OnPngEncoded,
                       weak_factory_.GetWeakPtr()));
  }

  void OnPngEncoded(std::optional<std::vector<uint8_t>> png) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!png || png->empty() || png->size() > kMaximumChallengePngBytes ||
        base::TimeTicks::Now() >= deadline_) {
      Refuse("picture-too-large-or-unencodable");
      return;
    }
    png_ = std::move(*png);
    ResolveAgain(base::BindOnce(&FieldChallengeCaptureRun::OnResolvedAfter,
                                weak_factory_.GetWeakPtr()));
  }

  void OnResolvedAfter(std::optional<ResolvedNodeFacts> current) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!current || !SameChallengeTarget(before_, *current) ||
        !ResolveWebContents() || base::TimeTicks::Now() >= deadline_) {
      Refuse("target-moved-while-capturing");
      return;
    }
    FieldChallengePresentation presentation;
    if (before_.challenge_kind == ChallengeKind::kImage) {
      if (png_.empty()) {
        Refuse("no-picture-at-finish");
        return;
      }
      presentation.image_png = std::move(png_);
    } else if (before_.challenge_kind == ChallengeKind::kInteractive) {
      if (!highlight_) {
        Refuse("no-highlight-at-finish");
        return;
      }
      presentation.highlight = highlight_;
    } else {
      Refuse("not-a-drawable-challenge-at-finish");
      return;
    }
    Finish(std::move(presentation), nullptr);
  }

  void OnDeadline() { Refuse("deadline"); }

  // One refusal, named, to the log and to the caller. The clause is a
  // compiled-in literal and carries nothing about the page, the person or the
  // task, which is what lets it cross to a caller that will fold it into
  // something the task reads (decision 0215).
  void Refuse(const char* at) {
    LOG(WARNING) << "[taffy_challenge_capture_refused] at=" << at;
    Finish(std::nullopt, at);
  }

  void Finish(std::optional<FieldChallengePresentation> presentation,
              const char* refused_at) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (finished_) {
      return;
    }
    scoped_refptr<FieldChallengeCaptureRun> keep_alive(this);
    finished_ = true;
    weak_factory_.InvalidateWeakPtrs();
    if (completion_) {
      std::move(completion_).Run(std::move(presentation), refused_at);
    }
  }

  const raw_ptr<content::BrowserContext> browser_context_;
  const std::string tab_id_;
  const std::string node_id_;
  const ResolvedNodeFacts expected_;
  std::string frame_id_;
  std::string page_epoch_;
  std::string origin_;
  FieldChallengePresentationCallback completion_;
  ResolvedNodeFacts before_;
  base::TimeTicks deadline_;
  gfx::Rect capture_bounds_;
  gfx::Size output_size_;
  std::vector<uint8_t> png_;
  std::optional<NormalizedFieldHighlight> highlight_;
  bool finished_ = false;
  base::WeakPtrFactory<FieldChallengeCaptureRun> weak_factory_{this};
};

}  // namespace

base::OnceClosure StartFieldChallengePresentation(
    content::BrowserContext* browser_context,
    std::string tab_id,
    std::string node_id,
    ResolvedNodeFacts expected,
    FieldChallengePresentationCallback completion) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto run = base::MakeRefCounted<FieldChallengeCaptureRun>(
      browser_context, std::move(tab_id), std::move(node_id),
      std::move(expected), std::move(completion));
  run->Start();
  return base::BindOnce(&FieldChallengeCaptureRun::Cancel, std::move(run));
}

}  // namespace taffy

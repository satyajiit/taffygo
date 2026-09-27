// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/page_pdf_observation.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/page_pdf_observation_platform.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/renderer_text_rescan.h"
#include "taffy/components/intelligence/content/scrubbing_serializer.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

constexpr base::TimeDelta kMaximumObservationLifetime = base::Seconds(10);
constexpr uint32_t kNativeTextConfidencePpm = 1'000'000u;
constexpr uint32_t kTableHeuristicConfidencePpm = 650'000u;

PagePdfObservationPlatform*& PlatformSlot() {
  static PagePdfObservationPlatform* platform = nullptr;
  return platform;
}

bool BoundedIdentifier(std::string_view value) {
  return !value.empty() && value.size() <= core_mojom::kMaxIdentifierBytes;
}

bool LooksLikeTableRow(std::string_view line) {
  if (line.find('\t') != std::string_view::npos) {
    return true;
  }
  size_t cells = 0u;
  bool in_cell = false;
  size_t spaces = 0u;
  for (char character : line) {
    if (character == ' ') {
      if (in_cell) {
        ++cells;
        in_cell = false;
      }
      ++spaces;
      continue;
    }
    if (spaces >= 2u && cells >= 1u) {
      return true;
    }
    spaces = 0u;
    in_cell = true;
  }
  return false;
}

bool HasNonWhitespace(std::u16string_view text) {
  return std::ranges::any_of(text, [](char16_t character) {
    return character != 0u &&
           (character > 0x7fu ||
            !std::isspace(static_cast<unsigned char>(character)));
  });
}

size_t Utf8Prefix(std::string_view text, size_t maximum) {
  size_t end = std::min(text.size(), maximum);
  while (end > 0u && end < text.size() &&
         (static_cast<unsigned char>(text[end]) & 0xc0u) == 0x80u) {
    --end;
  }
  return end;
}

std::u16string_view Utf16Prefix(std::u16string_view text, size_t maximum) {
  if (text.size() <= maximum) {
    return text;
  }
  size_t end = maximum;
  if (end > 0u && text[end - 1u] >= 0xd800u && text[end - 1u] <= 0xdbffu) {
    --end;
  }
  return text.substr(0u, end);
}

class PagePdfObservationRun : public base::RefCounted<PagePdfObservationRun> {
 public:
  PagePdfObservationRun(content::BrowserContext* browser_context,
                        PageMediaObservationRequest request,
                        PageMediaObservationCompletion completion)
      : browser_context_(browser_context),
        request_(std::move(request)),
        completion_(std::move(completion)) {}

  void Start() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!IsValidRequest()) {
      Finish(nullptr);
      return;
    }
    content::WebContents* web_contents = ResolveLiveDocument();
    PagePdfObservationPlatform* platform = GetPagePdfObservationPlatform();
    if (!web_contents || !platform) {
      Finish(nullptr);
      return;
    }
    const base::TimeDelta lifetime =
        std::min(request_.deadline, kMaximumObservationLifetime);
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&PagePdfObservationRun::OnDeadline,
                       weak_factory_.GetWeakPtr()),
        lifetime);
    base::OnceClosure cancel = platform->ReadNativeText(
        web_contents, static_cast<uint32_t>(core_mojom::kMaxMediaPdfPages),
        static_cast<uint32_t>(core_mojom::kMaxMediaFactTextBytes),
        static_cast<uint32_t>(core_mojom::kMaxMediaFactTotalBytes),
        static_cast<uint32_t>(lifetime.InMilliseconds()),
        base::BindOnce(&PagePdfObservationRun::OnNativeText,
                       weak_factory_.GetWeakPtr()));
    if (!finished_) {
      native_cancel_ = std::move(cancel);
    } else if (cancel) {
      std::move(cancel).Run();
    }
  }

  void Cancel() {
    CancelNativeRead();
    Finish(nullptr);
  }

 private:
  friend class base::RefCounted<PagePdfObservationRun>;
  ~PagePdfObservationRun() = default;

  bool IsValidRequest() const {
    return browser_context_ && completion_ &&
           request_.kind == PageMediaObservationKind::kPdf &&
           BoundedIdentifier(request_.task_id) &&
           BoundedIdentifier(request_.effect_id) &&
           request_.service_generation != 0u &&
           BoundedIdentifier(request_.tab_id) &&
           BoundedIdentifier(request_.frame_id) &&
           BoundedIdentifier(request_.page_epoch) && !request_.node_id &&
           request_.minimum_graph_revision != 0u &&
           request_.deadline.is_positive();
  }

  content::WebContents* ResolveLiveDocument() const {
    TaffyPageIntelligenceHost* host =
        FindPageIntelligenceHost(browser_context_, request_.tab_id);
    content::WebContents* web_contents =
        host ? host->observed_web_contents() : nullptr;
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

  void OnNativeText(std::optional<PagePdfNativeTextResult> result) {
    native_cancel_.Reset();
    if (!result || !ResolveLiveDocument() || result->page_count == 0u ||
        result->pages.size() > result->page_count ||
        result->pages.size() > core_mojom::kMaxMediaPdfPages) {
      Finish(nullptr);
      return;
    }
    bool truncated =
        result->truncated || result->pages.size() < result->page_count;
    for (size_t page_index = 0u; page_index < result->pages.size();
         ++page_index) {
      if (!ProcessPage(page_index, result->pages[page_index])) {
        truncated = true;
        break;
      }
    }
    FinishPdf(truncated);
  }

  bool ProcessPage(size_t page_index, const PagePdfNativePageText& page) {
    saw_native_text_ |= HasNonWhitespace(page.text);
    const size_t maximum_code_units =
        static_cast<size_t>(core_mojom::kMaxMediaFactTextBytes);
    const bool input_truncated =
        page.truncated || page.text.size() > maximum_code_units;
    const std::u16string_view bounded =
        Utf16Prefix(page.text, maximum_code_units);
    const std::string bounded_utf8 = base::UTF16ToUTF8(bounded);
    const bool rescan_truncated =
        bounded_utf8.size() > ScrubbingSerializer::kMaxScannedCharacters;
    std::string safe = RescanRendererText(bounded_utf8, &rescan_);
    if (safe.empty()) {
      return true;
    }
    const std::string locator =
        "pdf:page/" + base::NumberToString(page_index + 1u);
    const size_t remaining = core_mojom::kMaxMediaFactTotalBytes - fact_bytes_;
    const size_t maximum =
        remaining > locator.size()
            ? std::min<size_t>(core_mojom::kMaxMediaFactTextBytes,
                               remaining - locator.size())
            : 0u;
    const size_t prefix = Utf8Prefix(safe, maximum);
    if (prefix == 0u ||
        !AppendFact(
            core_mojom::MediaFactKind::kPdfText,
            core_mojom::MediaEvidenceKind::kPdfTextLayer,
            safe.substr(0u, prefix), locator, 0u, static_cast<uint32_t>(prefix),
            page_index, 0u, kNativeTextConfidencePpm,
            input_truncated || rescan_truncated || prefix < safe.size())) {
      return false;
    }
    AppendTableRows(std::string_view(safe).substr(0u, prefix), locator,
                    page_index);
    return facts_.size() < core_mojom::kMaxMediaFacts &&
           fact_bytes_ < core_mojom::kMaxMediaFactTotalBytes;
  }

  bool AppendFact(core_mojom::MediaFactKind kind,
                  core_mojom::MediaEvidenceKind evidence,
                  std::string text,
                  const std::string& locator,
                  uint32_t source_start,
                  uint32_t source_end,
                  size_t page_index,
                  uint32_t row_index_plus_one,
                  uint32_t confidence_ppm,
                  bool truncated) {
    if (facts_.size() >= core_mojom::kMaxMediaFacts || text.empty() ||
        text.size() > core_mojom::kMaxMediaFactTextBytes ||
        locator.size() > core_mojom::kMaxMediaFactLocatorBytes ||
        text.size() + locator.size() >
            core_mojom::kMaxMediaFactTotalBytes - fact_bytes_) {
      return false;
    }
    fact_bytes_ += text.size() + locator.size();
    auto fact = core_mojom::MediaObservationFact::New();
    fact->kind = kind;
    fact->evidence = evidence;
    fact->text = std::move(text);
    fact->source_locator = locator;
    fact->source_start = source_start;
    fact->source_end = source_end;
    fact->page_index_plus_one = static_cast<uint32_t>(page_index + 1u);
    fact->row_index_plus_one = row_index_plus_one;
    fact->confidence_ppm = confidence_ppm;
    fact->truncated = truncated;
    facts_.push_back(std::move(fact));
    return true;
  }

  void AppendTableRows(std::string_view page,
                       const std::string& locator,
                       size_t page_index) {
    size_t line_start = 0u;
    uint32_t row = 0u;
    while (line_start < page.size()) {
      const size_t newline = page.find('\n', line_start);
      const size_t line_end =
          newline == std::string_view::npos ? page.size() : newline;
      const std::string_view line =
          page.substr(line_start, line_end - line_start);
      if (!line.empty() && LooksLikeTableRow(line)) {
        ++row;
        if (!AppendFact(core_mojom::MediaFactKind::kPdfTableRow,
                        core_mojom::MediaEvidenceKind::kTableHeuristic,
                        std::string(line), locator,
                        static_cast<uint32_t>(line_start),
                        static_cast<uint32_t>(line_end), page_index, row,
                        kTableHeuristicConfidencePpm, false)) {
          facts_.back()->truncated = true;
          return;
        }
      }
      if (newline == std::string_view::npos) {
        return;
      }
      line_start = newline + 1u;
    }
  }

  void FinishPdf(bool truncated) {
    const bool has_safe_text = std::ranges::any_of(
        facts_, [](const core_mojom::MediaObservationFactPtr& fact) {
          return fact && fact->kind == core_mojom::MediaFactKind::kPdfText;
        });
    if (!ResolveLiveDocument() || (!has_safe_text && truncated) ||
        (saw_native_text_ && !has_safe_text)) {
      Finish(nullptr);
      return;
    }
    auto media = core_mojom::MediaObservationResult::New();
    media->kind = core_mojom::MediaObservationKind::kPdf;
    if (!has_safe_text) {
      media->scanned_pdf_ocr_required = true;
      Finish(std::move(media));
      return;
    }
    if (truncated && !facts_.empty()) {
      facts_.back()->truncated = true;
    }
    media->facts = std::move(facts_);
    media->has_meaningful_text = true;
    Finish(std::move(media));
  }

  void OnDeadline() {
    CancelNativeRead();
    Finish(nullptr);
  }

  void CancelNativeRead() {
    if (native_cancel_) {
      std::move(native_cancel_).Run();
    }
  }

  void Finish(core_mojom::MediaObservationResultPtr result) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (finished_) {
      return;
    }
    scoped_refptr<PagePdfObservationRun> keep_alive(this);
    finished_ = true;
    weak_factory_.InvalidateWeakPtrs();
    native_cancel_.Reset();
    if (completion_) {
      std::move(completion_)
          .Run(std::move(result), rescan_.redacted_span_count);
    }
  }

  raw_ptr<content::BrowserContext> browser_context_;
  PageMediaObservationRequest request_;
  PageMediaObservationCompletion completion_;
  base::OnceClosure native_cancel_;
  bool finished_ = false;
  bool saw_native_text_ = false;
  size_t fact_bytes_ = 0u;
  std::vector<core_mojom::MediaObservationFactPtr> facts_;
  RescanTally rescan_;
  base::WeakPtrFactory<PagePdfObservationRun> weak_factory_{this};
};

}  // namespace

void InstallPagePdfObservationPlatform(PagePdfObservationPlatform* platform) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(platform);
  CHECK(!PlatformSlot() || PlatformSlot() == platform);
  PlatformSlot() = platform;
}

PagePdfObservationPlatform* GetPagePdfObservationPlatform() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return PlatformSlot();
}

base::OnceClosure StartPagePdfObservation(
    content::BrowserContext* browser_context,
    PageMediaObservationRequest request,
    PageMediaObservationCompletion completion) {
  auto run = base::MakeRefCounted<PagePdfObservationRun>(
      browser_context, std::move(request), std::move(completion));
  run->Start();
  return base::BindOnce(&PagePdfObservationRun::Cancel, std::move(run));
}

}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/page_video_caption_facts.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "taffy/components/intelligence/content/renderer_text_rescan.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

size_t Utf8PrefixLength(std::string_view text, size_t maximum) {
  size_t end = std::min(text.size(), maximum);
  while (end > 0u && end < text.size() &&
         (static_cast<unsigned char>(text[end]) & 0xc0u) == 0x80u) {
    --end;
  }
  return end;
}

}  // namespace

bool PopulateVideoCaptionFacts(const mojom::MediaTargetResult& inspected,
                               core_mojom::MediaObservationResult& media,
                               RescanTally* rescan) {
  if (inspected.loaded_caption_cues.size() > core_mojom::kMaxMediaFacts) {
    return false;
  }
  size_t total_bytes = 0u;
  for (const mojom::MediaCaptionCuePtr& cue : inspected.loaded_caption_cues) {
    if (!cue || cue->track_index_plus_one == 0u ||
        cue->cue_index_plus_one == 0u || cue->text.empty() ||
        cue->text.size() > core_mojom::kMaxMediaFactTextBytes ||
        cue->timestamp_end_ms <= cue->timestamp_start_ms) {
      return false;
    }
    std::string safe = RescanRendererText(cue->text, rescan);
    if (base::TrimWhitespaceASCII(safe, base::TRIM_ALL).empty()) {
      continue;
    }
    const std::string locator =
        "video:track/" + base::NumberToString(cue->track_index_plus_one) +
        "/cue/" + base::NumberToString(cue->cue_index_plus_one);
    if (locator.size() > core_mojom::kMaxMediaFactLocatorBytes ||
        total_bytes >= core_mojom::kMaxMediaFactTotalBytes ||
        locator.size() > core_mojom::kMaxMediaFactTotalBytes - total_bytes) {
      if (!media.facts.empty()) {
        media.facts.back()->truncated = true;
      }
      break;
    }
    const size_t remaining =
        core_mojom::kMaxMediaFactTotalBytes - total_bytes - locator.size();
    const size_t maximum =
        std::min<size_t>(core_mojom::kMaxMediaFactTextBytes, remaining);
    const size_t prefix = Utf8PrefixLength(safe, maximum);
    if (prefix == 0u) {
      if (!media.facts.empty()) {
        media.facts.back()->truncated = true;
      }
      break;
    }
    auto fact = core_mojom::MediaObservationFact::New();
    fact->kind = core_mojom::MediaFactKind::kTranscript;
    fact->evidence = core_mojom::MediaEvidenceKind::kCaptionTrack;
    fact->text = safe.substr(0u, prefix);
    fact->source_locator = locator;
    fact->source_end = static_cast<uint32_t>(prefix);
    fact->timestamp_start_ms = cue->timestamp_start_ms;
    fact->timestamp_end_ms = cue->timestamp_end_ms;
    fact->confidence_ppm = 1'000'000u;
    fact->truncated = prefix < safe.size();
    total_bytes += prefix + locator.size();
    media.facts.push_back(std::move(fact));
  }
  if (!media.facts.empty()) {
    media.has_meaningful_text = true;
    return true;
  }
  constexpr std::string_view kUnavailable =
      "No usable loaded caption or subtitle cues were available; this result "
      "contains visual frame evidence only.";
  auto fact = core_mojom::MediaObservationFact::New();
  fact->kind = core_mojom::MediaFactKind::kMetadata;
  fact->evidence = core_mojom::MediaEvidenceKind::kDom;
  fact->text = std::string(kUnavailable);
  fact->source_locator = "video:captions";
  fact->confidence_ppm = 1'000'000u;
  media.facts.push_back(std::move(fact));
  return true;
}

}  // namespace taffy

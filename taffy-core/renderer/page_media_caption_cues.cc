// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/page_media_caption_cues.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "content/public/common/isolated_world_ids.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "v8/include/v8.h"

namespace taffy {
namespace {

// This product claims Content's first custom-embedder slot. Chrome begins its
// own worlds at the following value and assigns DOM Distiller to Chrome's
// internal world in a profile, so this ID is neither an extension world nor a
// page world in the TaffyGo renderer process.
constexpr int32_t kMediaInspectionWorld =
    content::ISOLATED_WORLD_ID_CONTENT_END;
constexpr size_t kMaximumCueCount = 64u;
constexpr size_t kMaximumCueTextBytes = 8u * 1024u;
constexpr size_t kMaximumJsonBytes = 512u * 1024u;

// This function is compiled in a product-owned isolated world, then invoked
// with the already-resolved WebElement as its receiver. Its loops and strings
// are bounded before anything is converted out of V8. It deliberately never
// changes TextTrack.mode: a disabled/unloaded track is unavailable, not an
// invitation to start a hidden fetch.
constexpr char kReadLoadedCues[] = R"JS(
(function() {
  "use strict";
  const output = [];
  let remaining = 32768;
  try {
    const tracks = this.textTracks;
    const trackCount = Math.min(tracks.length, 8);
    for (let trackIndex = 0;
         trackIndex < trackCount && output.length < 64 && remaining > 0;
         ++trackIndex) {
      const track = tracks[trackIndex];
      if (track.kind !== "captions" && track.kind !== "subtitles") continue;
      let cues;
      try { cues = track.cues; } catch (_) { continue; }
      if (!cues) continue;
      const cueCount = Math.min(cues.length, 64);
      for (let cueIndex = 0;
           cueIndex < cueCount && output.length < 64 && remaining > 0;
           ++cueIndex) {
        const cue = cues[cueIndex];
        if (!cue || typeof cue.text !== "string" || cue.text.length === 0)
          continue;
        const start = Math.round(cue.startTime * 1000);
        const end = Math.round(cue.endTime * 1000);
        if (!Number.isSafeInteger(start) || !Number.isSafeInteger(end) ||
            start < 0 || end <= start) continue;
        const text = cue.text.slice(0, Math.min(1024, remaining));
        remaining -= text.length;
        output.push([trackIndex + 1, cueIndex + 1, text,
                     String(start), String(end)]);
      }
    }
  } catch (_) {}
  return JSON.stringify(output);
})
)JS";

std::optional<std::string> RunExtractor(blink::WebLocalFrame* frame,
                                        blink::WebElement element) {
  if (!frame || element.IsNull()) {
    return std::nullopt;
  }
  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  if (!isolate) {
    return std::nullopt;
  }
  v8::HandleScope handles(isolate);
  v8::Local<v8::Value> function_value =
      frame->ExecuteScriptInIsolatedWorldAndReturnValue(
          kMediaInspectionWorld,
          blink::WebScriptSource(blink::WebString::FromUtf8(kReadLoadedCues)),
          blink::BackForwardCacheAware::kAllow);
  if (function_value.IsEmpty() || !function_value->IsFunction()) {
    return std::nullopt;
  }
  v8::Local<v8::Context> context =
      frame->GetScriptContextFromWorldId(isolate, kMediaInspectionWorld);
  if (context.IsEmpty()) {
    return std::nullopt;
  }
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Value> receiver = element.ToV8Value(isolate);
  v8::Local<v8::Value> value;
  v8::TryCatch try_catch(isolate);
  if (receiver.IsEmpty() ||
      !function_value.As<v8::Function>()
           ->Call(context, receiver, 0, nullptr)
           .ToLocal(&value) ||
      !value->IsString()) {
    return std::nullopt;
  }
  v8::String::Utf8Value utf8(isolate, value);
  if (!*utf8 || utf8.length() < 0 ||
      static_cast<size_t>(utf8.length()) > kMaximumJsonBytes) {
    return std::nullopt;
  }
  return std::string(*utf8, static_cast<size_t>(utf8.length()));
}

std::vector<mojom::MediaCaptionCuePtr> ParseCues(const std::string& json) {
  std::vector<mojom::MediaCaptionCuePtr> output;
  const std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC, 4);
  if (!parsed || !parsed->is_list() ||
      parsed->GetList().size() > kMaximumCueCount) {
    return output;
  }
  for (const base::Value& value : parsed->GetList()) {
    if (!value.is_list() || value.GetList().size() != 5u) {
      return {};
    }
    const base::ListValue& row = value.GetList();
    const std::optional<int> track = row[0].GetIfInt();
    const std::optional<int> cue = row[1].GetIfInt();
    const std::string* text = row[2].GetIfString();
    const std::string* start_text = row[3].GetIfString();
    const std::string* end_text = row[4].GetIfString();
    uint64_t start = 0u;
    uint64_t end = 0u;
    if (!track || *track <= 0 || !cue || *cue <= 0 || !text || text->empty() ||
        text->size() > kMaximumCueTextBytes || !start_text || !end_text ||
        !base::StringToUint64(*start_text, &start) ||
        !base::StringToUint64(*end_text, &end) || end <= start) {
      return {};
    }
    auto result = mojom::MediaCaptionCue::New();
    result->track_index_plus_one = static_cast<uint32_t>(*track);
    result->cue_index_plus_one = static_cast<uint32_t>(*cue);
    result->text = *text;
    result->timestamp_start_ms = start;
    result->timestamp_end_ms = end;
    output.push_back(std::move(result));
  }
  return output;
}

}  // namespace

std::vector<mojom::MediaCaptionCuePtr> ExtractLoadedCaptionCues(
    blink::WebLocalFrame* frame,
    blink::WebElement element) {
  const std::optional<std::string> json =
      RunExtractor(frame, std::move(element));
  return json ? ParseCues(*json) : std::vector<mojom::MediaCaptionCuePtr>();
}

}  // namespace taffy

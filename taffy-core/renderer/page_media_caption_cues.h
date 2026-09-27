// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_PAGE_MEDIA_CAPTION_CUES_H_
#define TAFFY_RENDERER_PAGE_MEDIA_CAPTION_CUES_H_

#include <vector>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "third_party/blink/public/web/web_element.h"

namespace blink {
class WebLocalFrame;
}  // namespace blink

namespace taffy {

// Reads only already-loaded caption/subtitle cues from one exact video
// element. Disabled tracks remain disabled and no network request is started.
// The browser treats every returned string as untrusted and rescans it.
std::vector<mojom::MediaCaptionCuePtr> ExtractLoadedCaptionCues(
    blink::WebLocalFrame* frame,
    blink::WebElement element);

}  // namespace taffy

#endif  // TAFFY_RENDERER_PAGE_MEDIA_CAPTION_CUES_H_

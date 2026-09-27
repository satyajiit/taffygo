// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_DOM_CONTENT_METADATA_H_
#define TAFFY_RENDERER_ADAPTERS_DOM_CONTENT_METADATA_H_

namespace blink {
class WebElement;
}  // namespace blink

// DOM-only inputs to the renderer's content-metadata module. Keeping style
// resolution here leaves the detector itself Blink-free and unit-testable.

namespace taffy::dom_content_metadata {

bool IsHiddenByStyle(blink::WebElement element);
bool IsUserGeneratedRegion(const blink::WebElement& element);

}  // namespace taffy::dom_content_metadata

#endif  // TAFFY_RENDERER_ADAPTERS_DOM_CONTENT_METADATA_H_

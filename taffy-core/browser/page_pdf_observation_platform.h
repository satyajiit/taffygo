// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PAGE_PDF_OBSERVATION_PLATFORM_H_
#define TAFFY_BROWSER_PAGE_PDF_OBSERVATION_PLATFORM_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"

namespace content {
class WebContents;
}  // namespace content

namespace taffy {

// Native text returned by the platform PDF viewer. The platform bounds both
// each page and the aggregate before crossing its in-process seam. These are
// still untrusted page facts: the common observation run rescans and redacts
// them before anything may leave the browser process.
struct PagePdfNativePageText {
  std::u16string text;
  bool truncated = false;
};

struct PagePdfNativeTextResult {
  uint32_t page_count = 0u;
  std::vector<PagePdfNativePageText> pages;
  bool truncated = false;
};

using PagePdfNativeTextCompletion =
    base::OnceCallback<void(std::optional<PagePdfNativeTextResult>)>;

// Android's PDF page is a native page backed by AndroidX rather than
// components/pdf. The Chrome-typed JNI edge implements this interface and
// installs its process-lifetime singleton when the product window registers.
// The portable browser library therefore never names TabAndroid or a Java
// class and remains linkable by content-shell tests.
class PagePdfObservationPlatform {
 public:
  virtual ~PagePdfObservationPlatform() = default;

  virtual base::OnceClosure ReadNativeText(
      content::WebContents* web_contents,
      uint32_t maximum_pages,
      uint32_t maximum_code_units_per_page,
      uint32_t maximum_total_code_units,
      uint32_t maximum_wait_milliseconds,
      PagePdfNativeTextCompletion completion) = 0;
};

void InstallPagePdfObservationPlatform(PagePdfObservationPlatform* platform);
PagePdfObservationPlatform* GetPagePdfObservationPlatform();

}  // namespace taffy

#endif  // TAFFY_BROWSER_PAGE_PDF_OBSERVATION_PLATFORM_H_

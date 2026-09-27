// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PAGE_PDF_OBSERVATION_H_
#define TAFFY_BROWSER_PAGE_PDF_OBSERVATION_H_

#include "taffy/browser/page_media_observation.h"

namespace taffy {

// Native-text-only PDF intelligence. Desktop asks Chromium's PDF helper;
// Android asks the exact AndroidX PdfDocument already owned by its visible
// native page. Neither path asks for document bytes. A fully inspected
// document with no usable native text returns the explicit scanned-PDF OCR
// boundary; a page-budget-limited document cannot make that claim.
base::OnceClosure StartPagePdfObservation(
    content::BrowserContext* browser_context,
    PageMediaObservationRequest request,
    PageMediaObservationCompletion completion);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PAGE_PDF_OBSERVATION_H_

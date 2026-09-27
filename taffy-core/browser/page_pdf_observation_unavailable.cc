// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/callback_helpers.h"
#include "taffy/browser/page_pdf_observation.h"

namespace taffy {

base::OnceClosure StartPagePdfObservation(
    content::BrowserContext*,
    PageMediaObservationRequest,
    PageMediaObservationCompletion completion) {
  if (completion) {
    std::move(completion).Run(nullptr, 0u);
  }
  return base::DoNothing();
}

}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_PDF_INTELLIGENCE_BRIDGE_H_
#define TAFFY_BROWSER_ANDROID_PDF_INTELLIGENCE_BRIDGE_H_

namespace taffy {

// Installs the process-lifetime Android implementation of the portable PDF
// observation platform. Repeated calls are idempotent.
void InstallAndroidPdfIntelligenceBridge();

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_PDF_INTELLIGENCE_BRIDGE_H_

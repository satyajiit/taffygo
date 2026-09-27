// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_TASK_NAVIGATION_AUTHORITY_CHROME_H_
#define TAFFY_BROWSER_ANDROID_TASK_NAVIGATION_AUTHORITY_CHROME_H_

namespace taffy {

// Installs ChromeNavigationUIData as the product carrier for exact task
// navigation authority. Called from the product-only window registration
// edge; content-shell and unit binaries never link this implementation.
void InstallChromeTaskNavigationAuthorityPlatform();

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_TASK_NAVIGATION_AUTHORITY_CHROME_H_

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import org.chromium.chrome.browser.profiles.Profile

/** Narrow Chromium preference port exposed only to the host test module. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
interface ChromiumDownloadLocationStore {
    fun write(profile: Profile, path: String)
    fun read(profile: Profile): String
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import org.chromium.chrome.browser.profiles.Profile

/** Browser-owned lookup for the Window-capable graph attached to a regular profile. */
interface TaffyProfileRuntimeProvider {
    /** Returns the regular runtime owned by Chromium's profile-keyed service. */
    fun requireRegularRuntime(profile: Profile): TaffyProfileRuntime
}

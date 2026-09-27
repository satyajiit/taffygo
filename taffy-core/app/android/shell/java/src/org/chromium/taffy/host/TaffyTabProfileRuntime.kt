// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import java.io.Closeable
import org.chromium.chrome.browser.tab.Tab

/** Internal profile owner shared only by regular and private movable-Tab graphs. */
internal interface TaffyTabProfileRuntime : Closeable {
    fun requireTab(tab: Tab): TaffyTabComponentOwner
}

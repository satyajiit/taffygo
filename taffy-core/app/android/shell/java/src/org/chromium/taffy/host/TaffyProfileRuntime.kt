// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.app.Activity
import com.taffygo.browser.ui.app.TaffyWindowComponent
import com.taffygo.browser.ui.core.browser.BrowserMediator
import org.chromium.chrome.browser.tabmodel.TabModelSelector

/** Regular-profile lifetime. Private profiles never expose a Window graph. */
interface TaffyProfileRuntime {
    /** Builds the sibling window graph with a browser-owned opaque identity. */
    fun openWindow(
        activity: Activity,
        windowId: Int,
        selector: TabModelSelector,
        browserMediator: BrowserMediator,
    ): TaffyWindowComponent
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.page.PageIntelligenceClient
import java.io.Closeable

/** One bounded page client and the exact transport owner that closes it. */
internal data class TaffyOwnedPageIntelligenceClient(
    val client: PageIntelligenceClient,
    val transport: Closeable,
)

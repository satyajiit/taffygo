// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import kotlinx.coroutines.flow.StateFlow

/**
 * The complete feature seam for a profile's person-started downloads.
 *
 * Snapshots are bounded and immutable. [perform] means the request was handed
 * to the profile adapter after it rechecked the current advertised actions;
 * false means the row or action was already stale. Completion remains an
 * asynchronous fact published in the next [snapshot].
 */
interface DownloadRepository {
    val snapshot: StateFlow<DownloadSnapshot>

    suspend fun perform(id: DownloadId, action: DownloadAction): Boolean
}

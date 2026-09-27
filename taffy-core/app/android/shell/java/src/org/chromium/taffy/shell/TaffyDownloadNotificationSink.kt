// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord

/** Platform edge for one regular profile's download notifications. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
interface TaffyDownloadNotificationSink {
    /** Posts or updates one notification, and reports whether Android accepted it. */
    fun show(profileToken: String, record: DownloadRecord): Boolean

    /** Removes the notification with the same opaque profile/download identity. */
    fun cancel(profileToken: String, id: DownloadId)

    /** Removes process-restored notifications absent from a complete provider snapshot. */
    fun reconcile(profileToken: String, retainedIds: Set<DownloadId>)

    /** Removes every notification owned by a closing regular profile. */
    fun cancelProfile(profileToken: String)
}

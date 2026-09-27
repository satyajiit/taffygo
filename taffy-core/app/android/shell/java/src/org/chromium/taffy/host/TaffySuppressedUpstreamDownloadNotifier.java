// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host;

import org.chromium.chrome.browser.download.DownloadInfo;
import org.chromium.chrome.browser.download.DownloadNotifier;
import org.chromium.components.offline_items_collection.ContentId;
import org.chromium.components.offline_items_collection.PendingState;

/** No-op presentation sink installed when TaffyGo owns the one download-notification surface. */
final class TaffySuppressedUpstreamDownloadNotifier implements DownloadNotifier {
    @Override
    public void notifyDownloadSuccessful(
            DownloadInfo info,
            long systemDownloadId,
            boolean canResolve,
            boolean isSupportedMimeType) {}

    @Override
    public void notifyDownloadFailed(DownloadInfo info) {}

    @Override
    public void notifyDownloadProgress(
            DownloadInfo info, long startTimeInMillis, boolean canDownloadWhileMetered) {}

    @Override
    public void notifyDownloadPaused(DownloadInfo info) {}

    @Override
    public void notifyDownloadInterrupted(
            DownloadInfo info, boolean isAutoResumable, @PendingState int pendingState) {}

    @Override
    public void notifyDownloadCanceled(ContentId id) {}

    @Override
    public void removeDownloadNotification(int notificationId, DownloadInfo info) {}
}

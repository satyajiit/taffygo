// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.feature.downloads.DownloadRepository
import dagger.Binds
import dagger.Module

/** Production adapter at the download feature seam. */
@Module
interface DownloadPlatformBindings {
    @Binds
    @TaffyWindowScope
    fun repository(adapter: BrowserDownloadRepository): DownloadRepository
}

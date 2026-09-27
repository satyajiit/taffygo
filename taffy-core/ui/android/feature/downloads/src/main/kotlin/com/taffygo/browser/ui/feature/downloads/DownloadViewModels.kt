// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.assets.TaffyPartsRepository
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** Dagger creators owned by the download feature. */
@Module
object DownloadViewModels {
    @Provides
    @IntoMap
    @ViewModelKey(DownloadsViewModel::class)
    fun downloads(
        repository: DownloadRepository,
        parts: TaffyPartsRepository,
        analytics: AnalyticsClient,
        dispatchers: AppDispatchers,
    ): ViewModelCreator = ViewModelCreator {
        DownloadsViewModel(repository, parts, analytics, dispatchers)
    }
}

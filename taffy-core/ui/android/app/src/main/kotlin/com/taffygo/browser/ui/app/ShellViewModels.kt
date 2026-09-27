// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.ui.ViewModelCreator
import com.taffygo.browser.ui.core.ui.ViewModelKey
import dagger.Module
import dagger.Provides
import dagger.multibindings.IntoMap

/** The window creator for the browser shell itself. */
@Module
object ShellViewModels {
    @Provides
    @IntoMap
    @ViewModelKey(ShellViewModel::class)
    fun shell(preferences: UserPreferencesRepository): ViewModelCreator =
        ViewModelCreator { savedState -> ShellViewModel(preferences, savedState) }

    @Provides
    @IntoMap
    @ViewModelKey(BackupViewModel::class)
    fun backup(host: BackupWindowHost, analytics: AnalyticsClient): ViewModelCreator =
        ViewModelCreator { BackupViewModel(host, analytics) }
}

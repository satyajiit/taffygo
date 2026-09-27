// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.api.PlatformPermissionRequester
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import dagger.Binds
import dagger.Module

/** Binds the one Window-owned Android permission adapter to portable UI callers. */
@Module
abstract class PermissionPlatformBindings {
    @Binds
    @TaffyWindowScope
    abstract fun requester(adapter: AndroidPermissionAdapter): PlatformPermissionRequester
}

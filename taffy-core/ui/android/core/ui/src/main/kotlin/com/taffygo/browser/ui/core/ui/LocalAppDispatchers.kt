// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.ProvidableCompositionLocal
import androidx.compose.runtime.staticCompositionLocalOf
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.CommonGraph

/** Profile dispatchers for Compose work; previews receive the safe common default. */
val LocalAppDispatchers: ProvidableCompositionLocal<AppDispatchers> =
    staticCompositionLocalOf(CommonGraph::dispatchers)

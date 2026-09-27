// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common.di

import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.CoroutineFailureScope
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import javax.inject.Inject

@TaffyProcessScope
class TaffyProcessLifetime @Inject constructor(
    dispatchers: AppDispatchers,
    failureSink: CoroutineFailureSink,
) : TaffyLifetime(dispatchers, failureSink, CoroutineFailureScope.PROCESS)

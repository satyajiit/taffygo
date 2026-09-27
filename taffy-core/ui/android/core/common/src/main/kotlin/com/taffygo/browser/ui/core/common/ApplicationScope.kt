// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common

import javax.inject.Qualifier

/**
 * The one coroutine scope that outlives every screen.
 *
 * Structured concurrency has no exceptions in TaffyGo-owned Kotlin: work that
 * belongs to a screen runs in `viewModelScope`, and work that genuinely
 * outlives one runs in this injected scope. `GlobalScope` appears nowhere,
 * because a job nobody owns is a job nobody cancels (decision 0014 item 5).
 */
@Qualifier
@Retention(AnnotationRetention.BINARY)
annotation class ApplicationScope

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel

/** Runtime half of a Dagger-provided view-model factory. */
fun interface ViewModelCreator {
    /** Construct one destination instance from restored, bounded arguments. */
    fun create(savedState: SavedStateHandle): ViewModel
}

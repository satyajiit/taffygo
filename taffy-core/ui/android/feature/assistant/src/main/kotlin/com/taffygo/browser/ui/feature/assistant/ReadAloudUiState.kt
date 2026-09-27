// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

/** Transient playback state for the final, already-visible assistant answer. */
sealed interface ReadAloudUiState {
    data object Idle : ReadAloudUiState
    data object Preparing : ReadAloudUiState
    data object Speaking : ReadAloudUiState
    data object Error : ReadAloudUiState
}

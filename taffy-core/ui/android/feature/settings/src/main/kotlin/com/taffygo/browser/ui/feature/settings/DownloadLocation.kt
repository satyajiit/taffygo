// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Display-safe download destination; its filesystem path stays in the browser adapter. */
data class DownloadLocation(
    val id: String,
    val kind: GeneralSettingsRepository.DownloadLocation.Kind,
)

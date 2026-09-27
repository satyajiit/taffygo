// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

/** One self-contained onboarding demonstration. */
internal data class ShowcaseSlide(
    val title: String,
    val question: String,
    val videoDescription: String,
    val video: ShowcaseVideo,
)

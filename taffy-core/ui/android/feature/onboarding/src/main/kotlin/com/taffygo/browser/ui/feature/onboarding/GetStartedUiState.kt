// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.model.LocalProfile

/** Screen SCR-007 — Get started. */
data class GetStartedUiState(
    /**
     * What the name field holds. Empty is the ordinary state: decision 0201
     * makes the name optional, and continuing without one leaves it unset.
     *
     * Held verbatim rather than normalized, because the store trims and a
     * field fed back from it would swallow the space between two words as
     * they were typed — see [LocalProfile.boundedDisplayName], which is the
     * bound this field does apply.
     */
    val name: String = "",
    /** The face chosen so far. The monogram is a choice, not an absence. */
    val avatar: LocalAvatar = LocalAvatar.Monogram,
    /** Whether the data sheet is up. */
    val dataSheetOpen: Boolean = false,
    /** A continue this screen has acted on and has not finished. */
    val continuing: Boolean = false,
) {
    /**
     * The letters the picture draws, read off the name as it is typed.
     *
     * One rule for the monogram, owned by [LocalProfile], so the letters a
     * person sees here are the letters You will draw afterwards.
     */
    val monogram: String
        get() = LocalProfile(displayName = LocalProfile.normalizedDisplayName(name)).monogram
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.model.LocalProfile

/** Screen SCR-410 — the person hub. */
data class YouUiState(
    /** The face this phone's profile wears, and the letters behind it. */
    val avatar: LocalAvatar = LocalAvatar.Monogram,
    val monogram: String = LocalProfile.MONOGRAM_FALLBACK,
    /**
     * The name this phone holds, from [LocalProfile]. Null when nobody set
     * one, which decision 0201 makes an ordinary state rather than a gap.
     */
    val displayName: String? = null,
    /**
     * What the name field is showing while it differs from [displayName].
     * Null means it is showing the stored name.
     *
     * A draft is needed because the store normalizes what it is given: echo a
     * trimmed write straight back into the field and the space a person types
     * between two words disappears as they type it, so a two-word name could
     * never be entered at all.
     */
    val nameDraft: String? = null,
    val detailsOpen: Boolean = false,
    val privateTab: Boolean = false,
    val timeAvailability: YouSurfaceAvailability = YouSurfaceAvailability.UNAVAILABLE,
    val timeHasSites: Boolean = false,
    val memoryAvailability: YouSurfaceAvailability = YouSurfaceAvailability.READY,
    val memoryCount: Int = 0,
    val signInsAvailability: YouSurfaceAvailability = YouSurfaceAvailability.UNAVAILABLE,
    val signInsCount: Int = 0,
    val detailsAvailability: YouSurfaceAvailability = YouSurfaceAvailability.READY,
    val detailsCount: Int = 0,
)

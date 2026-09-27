// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

/**
 * One answer to one composer suggestion request.
 *
 * Generated-contract vocabulary, unprojected, exactly as [AssetProgressReport]
 * is: the browser sends these two values and this carries them. Turning them
 * into something a screen renders is a repository's job.
 *
 * [text] is absent when there is no suggestion, and that is not the same fact
 * as a suggestion of no characters. A surface that folded the two together
 * would draw an empty ghost over the cursor for an answer that offered nothing,
 * so the difference is kept in the type rather than in a convention every
 * caller has to remember.
 */
data class ComposerCompletionReport(
    /** Which request this answers, as the surface minted it. */
    val requestId: String,
    /** The suggestion, or null when the request produced none. */
    val text: String?,
)

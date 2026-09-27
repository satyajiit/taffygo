// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * The manual-code fallback as screen SCR-416 draws it (decision 0095
 * section 2): what has been typed, whether the browser refused the last
 * attempt, and whether one is on its way.
 *
 * Carried on [ProviderSignInStage.Waiting] only for a flow that returns
 * through a redirect. A device-code flow shows a code to enter *elsewhere* and
 * has nothing to paste back, so its wait carries null and draws no field.
 *
 * The draft lives on this screen alone. It is never a fact the engine or the
 * browser holds: the browser is told the whole value once, when the person
 * presses the button, and answers yes or no.
 */
data class ManualCodeEntry(
    /** What the person has typed or pasted, whole and uninspected. */
    val draft: String = "",
    /**
     * The browser declined the last submission: no live flow accepted it, or
     * it was not a code for this one. Not an error — the person can try again
     * until the flow's deadline — so it clears the moment the draft changes.
     */
    val rejected: Boolean = false,
    /** A submission is with the browser and has not been answered. */
    val submitting: Boolean = false,
) {
    /** Whether pressing the button would hand something to the browser. */
    val submittable: Boolean get() = draft.isNotBlank() && !submitting
}

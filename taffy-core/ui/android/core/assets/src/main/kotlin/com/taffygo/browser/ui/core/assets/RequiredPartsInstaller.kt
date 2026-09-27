// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.assets

import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.TaffyPart
import com.taffygo.browser.ui.core.model.TaffyPartAvailability
import com.taffygo.browser.ui.core.model.TaffyPartsState
import kotlinx.coroutines.launch

/**
 * Asks for the parts the product cannot work without, from the moment the
 * profile exists. Which parts those are is a fact of [TaffyPartPurpose]: a
 * required purpose arrives on its own, on any live connection, and no screen
 * offers a person a button for it.
 *
 * ## Why this is not a screen's job
 *
 * It used to be one. Screens SCR-101 and SCR-102 each ran the same block in
 * their view model's `init`: watch the delivery snapshot, and the first time
 * it names a Python library that is not installed, ask for it once. That put
 * the product's own install behind a person reaching a particular screen,
 * and it asked exactly once per view model — so a request the core refused
 * because it was still starting was a request never made again, and the
 * screen waiting on it waited for the life of the process.
 *
 * The profile is the right owner. It exists before any window does, it
 * outlives every screen, and "the browser downloads what it needs when it
 * starts" is a statement about the product rather than about a surface.
 *
 * ## Why it re-asks, and what stops it looping
 *
 * A row is asked for again when the row itself has changed — a new version, a
 * new state, another attempt spent, a different hold. That is what makes a
 * refusal recoverable without making this a poller: an unchanged row is never
 * asked about twice, so a snapshot that keeps repeating the same fact costs
 * one command in total. A command that fails to reach the core is forgotten
 * rather than remembered, so the next snapshot asks again.
 *
 * The browser plans required artifacts itself at bootstrap, one transfer at a
 * time. This is the second ask, from the layer that can see whether the first
 * one stopped — and only then; [worthAsking] says why asking sooner is worse
 * than not asking at all.
 */
class RequiredPartsInstaller(
    private val parts: TaffyPartsRepository,
    private val lifetime: TaffyProfileLifetime,
) {
    private val asked = mutableSetOf<String>()
    private var started = false

    /** Begin watching. Calling this twice is the same as calling it once. */
    fun start() {
        if (started) return
        started = true
        lifetime.scope.launch {
            parts.state.collect(::consider)
        }
    }

    private suspend fun consider(snapshot: TaffyPartsState) {
        // Nothing has been described yet, so there is nothing to ask about.
        // Asking now would name a part the browser has not offered.
        if (!snapshot.answered) return
        for (part in snapshot.parts) {
            if (!part.purpose.required) continue
            if (!part.worthAsking()) continue
            val key = part.askKey()
            if (!asked.add(key)) continue
            if (parts.request(part.id) is TaffyResult.Failure) {
                // Not a want the core recorded, so it is not one this has
                // asked for. The next snapshot is free to ask again.
                asked.remove(key)
            }
        }
    }

    /**
     * Whether asking about this row could change anything.
     *
     * A row that is installed needs nothing. A row that is part-way here or
     * being checked is already moving. A row the browser has refused for good
     * is left alone: the delivery screen is where a person is told why.
     *
     * ## Why a row with no hold is left alone as well
     *
     * Because it is not stuck; it is queued. The browser plans every required
     * artifact when the profile starts and carries **one transfer at a time**,
     * so the second required row is legitimately absent, with nothing wrong,
     * for as long as the first one takes.
     *
     * Asking anyway did real damage. Every ask makes the core plan again, and
     * a plan is a pure function of what is installed — so an artifact that is
     * still downloading is not installed yet, and the new plan asks for it a
     * second time. The delivery plane holds one transfer and answers that
     * second request "interrupted", which the core records against the
     * artifact that is downloading perfectly well: one of its five attempts
     * spent, and a backoff started. Two or three asks during one download were
     * enough to leave the next required artifact parked behind a retry that
     * nothing wakes, and the country-flag pack never arrived at all.
     *
     * So this waits for a hold. A hold is the browser saying it tried and
     * stopped, which is the only state where a second ask is new information
     * rather than a duplicate of a request that is already in flight.
     */
    private fun TaffyPart.worthAsking(): Boolean = when (availability) {
        TaffyPartAvailability.READY,
        TaffyPartAvailability.PARTIAL,
        TaffyPartAvailability.CHECKING,
        -> false
        TaffyPartAvailability.MISSING -> hold?.canRetry == true
    }

    /**
     * What makes this row's situation different from the last one.
     *
     * The attempt count is in the key on purpose: it is the one field that
     * moves when the browser has tried and stopped, so including it is what
     * turns "ask once ever" into "ask once per thing that actually happened".
     */
    private fun TaffyPart.askKey(): String =
        "${id.value}@$version:${availability.label}:${hold?.label ?: "none"}:$attempts"
}

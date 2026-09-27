// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * The part of screen SCR-418 the screen itself owns.
 *
 * Everything else on that page is the core's published answer. This is what is
 * true only here and only now: what has been typed, whether a probe is out,
 * what the last one said, whether the corrected address has been accepted, and
 * whether the confirmation before a removal is showing.
 *
 * [proposal] is the field this screen exists for. A probe can prove the
 * OpenAI-shaped API at a different base from the one it was pointed at, and the
 * screen must then *offer* that address rather than quietly use it: the
 * register's whole question is "did this person type this?"
 * (decision 0096 section 1), and an address nobody typed cannot be answered
 * yes. So the proposal sits here as a separate fact from [address] until a
 * person answers it — accepting makes it [address] and leaves nothing to
 * propose, keeping sets [keptAsTyped] and leaves the caution standing. Neither
 * answer is assumed, which is why an unanswered proposal blocks the save.
 */
data class CustomEndpointDraft(
    /** The address as typed. Never edited by anything but a person. */
    val address: String = "",
    /**
     * Whether [address] has been answered here at all.
     *
     * Its own fact rather than "the field is not empty", for [nameTouched]'s
     * reason and one more: editing a provider starts the field at the address
     * the core registered, and the moment a person types the draft has to own
     * it — otherwise the next published snapshot puts the saved address back
     * under what they are typing.
     */
    val addressTouched: Boolean = false,
    /** What to call this provider in the roster. */
    val name: String = "",
    /**
     * The identity this page intends to save under, minted when the first probe
     * is spent and null before then.
     *
     * The probe names it so the verdict is filed on a row this screen can point
     * at, and the save reuses it so the verdict and the provider it produced
     * are the same thing (decision 0096 section 5). Held here rather than
     * recomputed at save time because a second minting could answer
     * differently — the roster moves between the two moments.
     */
    val providerId: String? = null,
    /**
     * Whether [name] has been typed into at all.
     *
     * Its own fact rather than "the field is not empty", because the name
     * field starts showing the roster's name for a provider being edited and
     * has to stop the moment a person touches it — otherwise the next
     * published snapshot puts the old name back under what they are typing,
     * and clearing the field would look like the edit was undone.
     */
    val nameTouched: Boolean = false,
    /**
     * The key for a server that asks for one, as typed. Empty is the ordinary
     * case and means this endpoint needs none.
     *
     * It lives here and nowhere else, exactly as screen SCR-415's key draft
     * does: it never reaches a repository as text, a log or an analytics
     * payload. The bytes handed to the secure store are built at the moment of
     * sealing and zeroed straight after, and this string stops being anything
     * the moment the page is left.
     */
    val key: String = "",
    /** Whether the key field is showing its characters rather than dots. */
    val keyRevealed: Boolean = false,
    /** Whether the advanced part of the page is open. */
    val advancedOpen: Boolean = false,
    /**
     * Whether this phone's secure store would not hold the key.
     *
     * Its own fact rather than a saving refusal, because the two are different
     * problems with different next steps: nothing was asked of the server and
     * nothing was written, and the thing to try again is the key.
     */
    val keySealRefused: Boolean = false,
    /** Whether a probe is out. */
    val probing: Boolean = false,
    /** What the last probe said, or null when none has been spent. */
    val outcome: CustomEndpointOutcome? = null,
    /** The base the probe proved, when it is not the address that was typed. */
    val proposal: String? = null,
    /**
     * Whether a proposal was answered by keeping the address as typed. Cleared
     * whenever the address changes, because the question is about that address.
     */
    val keptAsTyped: Boolean = false,
    /** Whether the one write is out. */
    val saving: Boolean = false,
    /** Whether the core refused the write. */
    val saveRefused: Boolean = false,
    /** Whether the deliberate second step before a removal is showing. */
    val confirmingDelete: Boolean = false,
    /** Whether the removal is running. */
    val deleting: Boolean = false,
) {
    /**
     * Whether a write is out and the fields must not move under it.
     *
     * A probe is deliberately not one. It changes nothing durable and can take
     * the best part of a minute, and freezing what somebody typed for that
     * long — on the one field they are most likely to want to correct — would
     * make the check feel like a lock. What makes that safe is the view
     * model's own guard: an answer about an address the field no longer holds
     * is dropped rather than shown.
     */
    val writing: Boolean get() = saving || deleting

    /** Whether anything at all is out, and no control may be pressed. */
    val busy: Boolean get() = probing || writing
}

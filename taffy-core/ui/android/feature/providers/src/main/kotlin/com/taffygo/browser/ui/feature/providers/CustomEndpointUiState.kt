// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * Screen SCR-418 — a model server the person runs themselves.
 *
 * Ollama on a laptop, LM Studio on a desktop, vLLM or llama.cpp on a box in
 * the next room. Every one of them is a **network address**, which is what
 * decision
 * `docs/decisions/0060-no-on-device-model-runtime-is-selected.md` leaves as the
 * only shape this can have: nothing runs a model on the phone, so this page is
 * never about on-device inference and must never read as though it were.
 *
 * The order of the fields is the order of the page and it is the argument the
 * page makes: what the address is, the key if the server asks for one, what is
 * there, what to call it, and only then the write. Nothing is saved before the
 * address has answered, because a provider filed with nothing behind it to
 * route to is decision 0096 section 4's whole complaint. The key sits in the
 * advanced part and is optional, because most servers a person runs themselves
 * ask for nothing and a required field would say the opposite.
 */
data class CustomEndpointUiState(
    /** Whether there is a page to draw, and what to say when there is not. */
    val status: Status = Status.LOADING,
    /** The provider being edited, or null while one is being added. */
    val endpointId: String? = null,
    /**
     * The address to show: what a person typed, or — until they touch the
     * field — the address the core has this provider registered at.
     */
    val address: String = "",
    /** The key for a server that asks for one, as typed. Empty asks for none. */
    val key: String = "",
    /** Whether the key field is showing its characters rather than dots. */
    val keyRevealed: Boolean = false,
    /** Whether the advanced part of the page is open. */
    val advancedOpen: Boolean = false,
    /** Whether this phone's secure store would not hold the key. */
    val keyStoreRefused: Boolean = false,
    /**
     * Whether the browser already holds a credential for this provider.
     *
     * Only ever true while editing, and it is what the empty key field means
     * there: the stored key stays as it is. A page that said nothing would be
     * asking a person to retype a key TaffyGo can never show them again.
     */
    val credentialHeld: Boolean = false,
    /** What this provider will be called in the roster. */
    val name: String = "",
    /** Why the address would be refused, or null when nothing can be seen wrong with it. */
    val refusal: CustomEndpointAddress.Refusal? = null,
    /** Whether this is plain http to a machine on the person's own network. */
    val cleartext: Boolean = false,
    /** Whether a probe is out. */
    val probing: Boolean = false,
    /** What the last probe answered, or null when none has been spent. */
    val outcome: CustomEndpointOutcome? = null,
    /**
     * The base the probe proved the model API at, when it is not the address
     * that was typed. Shown, never applied — see [CustomEndpointDraft].
     */
    val proposal: String? = null,
    /** Whether the proposal was answered by keeping the address as typed. */
    val keptAsTyped: Boolean = false,
    /** Whether the one write is out. */
    val saving: Boolean = false,
    /** Whether the core refused the write. */
    val saveRefused: Boolean = false,
    /** The host this provider is currently set to, when the core published one. */
    val currentHost: String? = null,
    /** The name of the model this provider is pinned to, when it carries one. */
    val pinnedModelName: String? = null,
    /** Whether the deliberate second step before a removal is showing. */
    val confirmingDelete: Boolean = false,
    /** Whether the removal is running. */
    val deleting: Boolean = false,
) {
    /** Whether this page is managing a provider that already exists. */
    val editing: Boolean get() = endpointId != null

    /**
     * Whether a write is out and the fields must not move under it. A probe is
     * not one — see [CustomEndpointDraft].
     */
    val writing: Boolean get() = saving || deleting

    /** Whether anything at all is out, and no control may be pressed. */
    val busy: Boolean get() = probing || writing

    /** Whether the address is worth spending a probe on. */
    val probeActionable: Boolean get() = refusal == null && address.isNotBlank() && !busy

    /**
     * Whether the proposal is still waiting on a person.
     *
     * This is the one thing on the page that holds the save shut without
     * anything being wrong: the address is fine, the server answered, and
     * TaffyGo has a question about which of two addresses is the one they
     * meant. Saving past an unanswered proposal would be answering it for
     * them.
     */
    val proposalUnanswered: Boolean get() = proposal != null && !keptAsTyped

    /** Whether the address answered and said what it is running. */
    val reached: CustomEndpointOutcome.Reached? get() = outcome as? CustomEndpointOutcome.Reached

    /**
     * Whether the write can be made.
     *
     * Nothing here asks a person to name a model any more: the probe carries
     * the models it read, so the save files what the server actually listed. A
     * server that listed none is saved with an empty roster, stated rather than
     * defaulted, because that is what it said.
     */
    val saveActionable: Boolean
        get() = reached != null &&
            refusal == null &&
            name.isNotBlank() &&
            !busy &&
            !proposalUnanswered

    /**
     * What the page has to say when it has no provider to draw.
     *
     * [UNKNOWN] is not an error, for the reason screen SCR-415 gives about the
     * same state: a provider can leave the roster between one snapshot and the
     * next, and a page opened from a stale back stack is then about something
     * the browser no longer carries.
     */
    enum class Status {
        /** The core has not published a roster yet. */
        LOADING,

        /** The roster arrived and does not carry the provider this page names. */
        UNKNOWN,

        /** There is a page to draw. */
        READY,
    }
}

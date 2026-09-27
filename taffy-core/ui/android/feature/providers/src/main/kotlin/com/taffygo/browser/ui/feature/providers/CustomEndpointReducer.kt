// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * The half of screen SCR-418 that changes nothing outside the screen.
 *
 * Typing, taking a preset, answering the proposal and opening or closing the
 * confirmation before a removal all move only what a person can see. They are
 * here so a host test can drive them without a core, and so the view model
 * holds only the three intents that actually reach the browser: the probe, the
 * one write, and the removal.
 *
 * The rule that runs through all of it: **anything that changes the address
 * throws away everything that was said about the old one.** A probe answer, a
 * proposal and a kept-as-typed answer are all statements about one exact
 * string, and leaving any of them standing beside a different string would
 * describe the previous address as though it were this one — which on this
 * screen is the difference between an endpoint that works and one that
 * answers every request with a 404.
 */
object CustomEndpointReducer {

    /** Fold one screen-local intent into the draft. */
    fun reduce(draft: CustomEndpointDraft, intent: CustomEndpointIntent): CustomEndpointDraft =
        when (intent) {
            is CustomEndpointIntent.ChangeAddress -> draft.withAddress(intent.typed)

            // A preset is typing, done for you. It goes through the same path
            // for the same reason: the address changed, so nothing said about
            // the last one survives.
            is CustomEndpointIntent.UsePreset -> draft.withAddress(intent.preset.address)

            is CustomEndpointIntent.ChangeName ->
                if (draft.writing) {
                    draft
                } else {
                    draft.copy(name = intent.typed, nameTouched = true, saveRefused = false)
                }

            // The probe answer deliberately survives a key change: a verdict
            // that said the server will list nothing without a credential is
            // the instruction to type this, and erasing it at the first
            // character would answer the person's question by hiding it. What
            // does go is the store's refusal, which was about the last key.
            is CustomEndpointIntent.ChangeKey ->
                if (draft.writing) {
                    draft
                } else {
                    draft.copy(key = intent.typed, keySealRefused = false, saveRefused = false)
                }

            CustomEndpointIntent.ToggleKeyVisible -> draft.copy(keyRevealed = !draft.keyRevealed)

            CustomEndpointIntent.ToggleAdvanced -> draft.copy(advancedOpen = !draft.advancedOpen)

            // The proposal becomes the address, and there is then nothing left
            // to propose — it is the base the probe proved, so the same
            // question asked of it answers no. The probe answer stays: it is
            // about the same server, reached through the base that server
            // actually serves the model API on.
            CustomEndpointIntent.AcceptProposal -> draft.proposal
                ?.let {
                    draft.copy(
                        address = it,
                        addressTouched = true,
                        proposal = null,
                        saveRefused = false,
                    )
                }
                ?: draft

            // The person insists on what they typed. The proposal stays in the
            // draft so the caution stays on the page; what changes is that the
            // question has been answered and the save is no longer held.
            CustomEndpointIntent.KeepAddress ->
                if (draft.proposal == null) draft else draft.copy(keptAsTyped = true)

            // Never while a removal is running: a confirmation that reopens
            // over a request already sent invites a second one.
            CustomEndpointIntent.AskDelete ->
                if (draft.deleting) draft else draft.copy(confirmingDelete = true)

            CustomEndpointIntent.CancelDelete -> draft.copy(confirmingDelete = false)

            else -> draft
        }

    private fun CustomEndpointDraft.withAddress(typed: String): CustomEndpointDraft =
        if (writing) {
            this
        } else {
            copy(
                address = typed,
                addressTouched = true,
                outcome = null,
                proposal = null,
                keptAsTyped = false,
                saveRefused = false,
            )
        }
}

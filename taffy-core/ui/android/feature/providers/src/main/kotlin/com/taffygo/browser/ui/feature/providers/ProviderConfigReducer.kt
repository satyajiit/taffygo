// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * The half of screen SCR-415 that changes nothing outside the screen.
 *
 * Four of the page's intents move only what the person can see: typing,
 * unmasking, asking to remove and changing their mind. They are here so a host
 * test can drive them without a repository, and so the view model contains only
 * the intents that actually reach the browser.
 *
 * The intents that do reach it are absent on purpose. A reducer that also
 * "saved" would have to model a call it cannot make, and the first thing to go
 * wrong would be a form reporting a key stored that never left the phone.
 */
object ProviderConfigReducer {

    /**
     * Fold one screen-local intent into the draft.
     *
     * Typing clears both the last failure and the last success: a form being
     * edited is neither refused nor connected, and leaving either standing
     * beside new text would describe the previous attempt as though it were
     * about this one. It does not clear while a call is out, because that draft
     * is the one being proved.
     */
    fun reduce(draft: ProviderConfigDraft, intent: ProviderConfigIntent): ProviderConfigDraft =
        when (intent) {
            is ProviderConfigIntent.ChangeKey ->
                if (draft.probing) {
                    draft
                } else {
                    draft.copy(key = intent.draft, problem = null, stored = false)
                }

            ProviderConfigIntent.ToggleKeyVisible -> draft.copy(revealed = !draft.revealed)

            // Never while a removal is running: a confirmation that reopens
            // over a request already sent invites a second one.
            ProviderConfigIntent.AskSignOut ->
                if (draft.signingOut) draft else draft.copy(confirmingSignOut = true)

            ProviderConfigIntent.CancelSignOut -> draft.copy(confirmingSignOut = false)

            else -> draft
        }
}

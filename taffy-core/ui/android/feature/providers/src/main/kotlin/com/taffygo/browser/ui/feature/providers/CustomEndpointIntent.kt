// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/** Everything screen SCR-418 can be asked to do. */
sealed interface CustomEndpointIntent {

    /** The address changed. Carries what was typed and nothing about it. */
    data class ChangeAddress(val typed: String) : CustomEndpointIntent

    /** Fill the address from one of the runtimes people usually run. */
    data class UsePreset(val preset: CustomEndpointAddress.Preset) : CustomEndpointIntent

    /** The name changed. */
    data class ChangeName(val typed: String) : CustomEndpointIntent

    /**
     * The key changed. Carries what was typed and nothing about it.
     *
     * Nothing said about the address is thrown away here, unlike
     * [ChangeAddress]: a check that came back asking for a credential is the
     * very sentence telling a person to type this, and clearing it while they
     * do would take away the instruction.
     */
    data class ChangeKey(val typed: String) : CustomEndpointIntent

    /** Show the key's characters, or go back to dots. */
    data object ToggleKeyVisible : CustomEndpointIntent

    /** Open or close the advanced part of the page. */
    data object ToggleAdvanced : CustomEndpointIntent

    /** Ask the address what it is (decision 0096 section 5). */
    data object Probe : CustomEndpointIntent

    /**
     * Take the corrected address the probe implied.
     *
     * The proposal becomes what was typed, and only a person can do this —
     * which is the whole of why a proposal is a separate field.
     */
    data object AcceptProposal : CustomEndpointIntent

    /** Keep the address exactly as typed and probe no further. */
    data object KeepAddress : CustomEndpointIntent

    /** Write the provider, its address and its models in one command (section 4). */
    data object Save : CustomEndpointIntent

    /** Go to this provider's models (SCR-417). */
    data object ChooseModel : CustomEndpointIntent

    /** Ask before removing the provider. */
    data object AskDelete : CustomEndpointIntent

    /** Remove it. Reachable only from the confirmation. */
    data object ConfirmDelete : CustomEndpointIntent

    /** Leave the provider alone. */
    data object CancelDelete : CustomEndpointIntent
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * The key half of screen SCR-415, as the screen draws it.
 *
 * Present only when the provider takes a key at all. A provider whose one way
 * in is a vendor sign-in gets no form: an input that saves a credential the
 * vendor does not issue is a control that cannot work, and the sign-in block
 * above it is already the instruction.
 *
 * The draft lives here and nowhere else. It never reaches a repository, a log
 * or an analytics payload, and the bytes handed to the secure store are built
 * at the moment of saving and zeroed straight after — this string is the
 * person's own text on their own screen and stops being anything the moment
 * they leave.
 */
data class ProviderKeyForm(
    /** What has been typed. */
    val draft: String,
    /** Whether the field is showing the characters rather than dots. */
    val revealed: Boolean,
    /** What can be told about the draft without calling anybody. */
    val verdict: Verdict,
    /** How far the prove-then-store sequence has got. */
    val stage: Stage,
    /** Why the last attempt did not store, in the product's own vocabulary. */
    val problem: ProviderKeyProblem?,
    /** Whether saving would replace a key that is already stored. */
    val replacing: Boolean,
) {
    /** Whether the form is waiting on the provider and must not be re-sent. */
    val busy: Boolean get() = stage == Stage.TESTING

    /** Whether pressing the action could achieve anything right now. */
    val actionable: Boolean get() = verdict == Verdict.PLAUSIBLE && !busy

    /**
     * Whether an indefinite verdict's second offer is on the table. Only an
     * indefinite answer earns it; a definitive refusal never renders it.
     */
    val offersSaveAnyway: Boolean get() = problem?.savableAnyway == true && !busy

    /**
     * The three stages of proving one key (decision 0083), and the whole of
     * what the action button says.
     *
     * There is no failed stage: a refusal is a [problem] beside an [IDLE]
     * form, because the next thing to do is correct the draft and the form has
     * to be usable to do it.
     */
    enum class Stage {
        /** Nothing is in flight. */
        IDLE,

        /** One bounded call is out and the answer has not come back. */
        TESTING,

        /** The provider answered on this key and the store holds it. */
        CONNECTED,
    }

    /**
     * What the form can tell about the draft on its own.
     *
     * [PREFIX_MISMATCH] exists because the catalog says what this vendor's keys
     * begin with (`ProviderPresentation.keyPrefix`), and a form that knows that
     * and spends a network call anyway makes a person wait to be told something
     * it could have said instantly. With no prefix served there are only two
     * answers, which is the honest reading of a catalog that said nothing.
     */
    enum class Verdict {
        /** Nothing but whitespace. */
        EMPTY,

        /** Not the shape this vendor's keys have. */
        PREFIX_MISMATCH,

        /** Worth spending a call on. Never a claim that the key works. */
        PLAUSIBLE,
    }
}

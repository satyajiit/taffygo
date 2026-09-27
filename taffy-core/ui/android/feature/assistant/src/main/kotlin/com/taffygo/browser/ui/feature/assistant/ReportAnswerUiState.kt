// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

/**
 * The sheet that reports one of Taffy's answers to the people who make
 * TaffyGo (decision 0253).
 *
 * TaffyGo runs no server, so a report is a draft: an email the person's own
 * app opens with the answer in it, or a public GitHub issue with a title and
 * nothing of the answer. This state holds what the draft is built from.
 */
data class ReportAnswerUiState(
    /** The answer being reported, or null while the sheet is closed. */
    val report: Report? = null,
    /**
     * True once the email draft found no app to open it, so the sheet names
     * the address instead of closing as though something happened.
     */
    val emailUnavailable: Boolean = false,
) {
    /** What an email draft carries about the answer. */
    data class Report(
        /** The answer as the person read it, cut at [MAX_REPORTED_ANSWER_CHARS]. */
        val answer: String,
        /** Whether [answer] was cut, which the draft then says. */
        val shortened: Boolean,
        /**
         * Every provider a request could reach when the report was opened.
         * The core does not say which one wrote a given answer, so the draft
         * names all of them rather than guess.
         */
        val providers: List<Provider>,
    )

    /** One provider, and the model the person chose for it when there is one. */
    data class Provider(val name: String, val model: String?)

    companion object {
        /**
         * The most of an answer a draft carries. A long answer stays readable
         * in a mail app and the address that carries it stays well inside
         * what Android hands between apps.
         */
        const val MAX_REPORTED_ANSWER_CHARS: Int = 4_000
    }
}

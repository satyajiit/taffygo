// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import java.util.Locale

/**
 * The words of an email draft, read from resources by the sheet and handed
 * in, so a host test can build the whole draft without a resource table.
 *
 * Every template is a positional format string from `strings.xml`.
 */
internal data class ReportAnswerDraftWords(
    /** `%1$s` the answer, `%2$s` the providers, `%3$s` the version. */
    val body: String,
    /** `%1$s` an answer that was cut, followed by a line saying so. */
    val shortened: String,
    /** `%1$s` a provider, `%2$s` the model the person chose for it. */
    val providerWithModel: String,
    /** `%1$s` a provider the person chose no model for. */
    val providerAlone: String,
    /** Stands in for a version or a provider list nobody can name. */
    val notKnown: String,
)

/**
 * The body of the email draft about [report]: a space for the person's own
 * note first, then the answer, the providers set up, and the version.
 *
 * The answer is an argument and never part of a template, so a `%` in what
 * Taffy wrote is copied as written.
 */
internal fun reportAnswerEmailBody(
    report: ReportAnswerUiState.Report,
    version: String?,
    words: ReportAnswerDraftWords,
    locale: Locale,
): String {
    val answer = if (report.shortened) {
        String.format(locale, words.shortened, report.answer)
    } else {
        report.answer
    }
    val providers = report.providers
        .joinToString(separator = "\n") { provider ->
            provider.model?.let { String.format(locale, words.providerWithModel, provider.name, it) }
                ?: String.format(locale, words.providerAlone, provider.name)
        }
        .ifEmpty { words.notKnown }
    return String.format(locale, words.body, answer, providers, version ?: words.notKnown)
}

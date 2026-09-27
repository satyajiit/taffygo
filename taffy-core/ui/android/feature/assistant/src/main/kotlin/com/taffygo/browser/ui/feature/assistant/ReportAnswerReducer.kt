// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.task.providerReadinessFacts
import taffy.core_api.CoreStatus

/**
 * The report sheet's reducer. [providers] is read from the core when the
 * sheet opens and is ignored by every other intent.
 */
internal fun reduceReportAnswer(
    state: ReportAnswerUiState,
    intent: ReportAnswerIntent,
    providers: List<ReportAnswerUiState.Provider>,
): ReportAnswerUiState = when (intent) {
    is ReportAnswerIntent.Open ->
        reportFor(intent.answer, providers)?.let { ReportAnswerUiState(report = it) } ?: state
    is ReportAnswerIntent.EmailDraft -> when {
        state.report == null -> state
        intent.opened -> ReportAnswerUiState()
        else -> state.copy(emailUnavailable = true)
    }
    is ReportAnswerIntent.OpenPublicIssue, ReportAnswerIntent.Dismiss -> ReportAnswerUiState()
}

/**
 * The report an answer makes, or null when there is no text to report — an
 * answer still waiting for its first words is not something a person read.
 *
 * The cut never splits a character that takes two UTF-16 units, so the draft
 * cannot end in half an emoji or half of a supplementary-plane letter.
 */
internal fun reportFor(
    answer: TaskAnswerProjection,
    providers: List<ReportAnswerUiState.Provider>,
): ReportAnswerUiState.Report? {
    val text = answer.segments.joinToString(separator = "")
    if (text.isBlank()) return null
    val limit = ReportAnswerUiState.MAX_REPORTED_ANSWER_CHARS
    if (text.length <= limit) {
        return ReportAnswerUiState.Report(answer = text, shortened = false, providers = providers)
    }
    val end = if (Character.isHighSurrogate(text[limit - 1])) limit - 1 else limit
    return ReportAnswerUiState.Report(
        answer = text.substring(0, end),
        shortened = true,
        providers = providers,
    )
}

/**
 * The providers a request could reach, each with the model the person chose
 * for it, in roster order.
 *
 * "Could reach" is the readiness rule's own answer, not a second copy of it.
 * A model the person chose that the roster no longer lists is reported as no
 * choice, because that is what a request then follows: the provider's own
 * order, the way the connected-providers screen says it.
 */
internal fun reportedProviders(status: CoreStatus): List<ReportAnswerUiState.Provider> {
    val answering = status.providerReadinessFacts().answeringProviderIds
    return status.provider_roster
        .filter { it.provider_id in answering }
        .map { row ->
            ReportAnswerUiState.Provider(
                name = row.display_name,
                model = row.selected_model_id?.let { pinned ->
                    status.provider_models.firstOrNull {
                        it.provider_id == row.provider_id && it.model_id == pinned
                    }?.display_name
                },
            )
        }
}

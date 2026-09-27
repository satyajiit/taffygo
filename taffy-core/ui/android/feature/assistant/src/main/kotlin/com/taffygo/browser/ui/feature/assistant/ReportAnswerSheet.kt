// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffyProjectContact
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.installedVersionName
import com.taffygo.browser.ui.core.ui.openEmailDraft
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The quiet way in, under an answer a person has finished reading.
 *
 * It is drawn only once the answer has stopped arriving and has text, because
 * a report is about something a person read, not something still being
 * written.
 */
@Composable
internal fun ReportAnswerButton(onClick: () -> Unit, modifier: Modifier = Modifier) {
    TextButton(onClick = onClick, modifier = modifier.testTag(REPORT_ANSWER_BUTTON_TEST_TAG)) {
        Text(
            text = taffyString(R.string.taffy_report_answer_action),
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/**
 * The report sheet: an email draft first, a public issue second, and what
 * each one does said before the person chooses (decision 0253).
 *
 * Nothing leaves the phone from here. Email hands a draft to the person's own
 * app, which sends it or not; the issue opens GitHub's form in a new tab with
 * a title and none of the answer, because an issue is public.
 */
@Composable
internal fun ReportAnswerSheet(
    state: ReportAnswerUiState,
    onIntent: (ReportAnswerIntent) -> Unit,
) {
    val report = state.report ?: return
    val context = LocalContext.current
    val subject = taffyString(R.string.taffy_report_answer_email_subject)
    val body = reportAnswerEmailBody(
        report = report,
        version = installedVersionName(context),
        words = ReportAnswerDraftWords(
            body = taffyString(R.string.taffy_report_answer_email_body),
            shortened = taffyString(R.string.taffy_report_answer_email_shortened),
            providerWithModel = taffyString(R.string.taffy_report_answer_email_provider_model),
            providerAlone = taffyString(R.string.taffy_report_answer_email_provider_alone),
            notKnown = taffyString(R.string.taffy_report_answer_email_not_known),
        ),
        locale = LocalConfiguration.current.locales[0],
    )
    val issueTitle = taffyString(R.string.taffy_report_answer_issue_title)
    TaffyBottomSheet(
        title = taffyString(R.string.taffy_report_answer_title),
        onDismissRequest = { onIntent(ReportAnswerIntent.Dismiss) },
        testTag = REPORT_ANSWER_SHEET_TEST_TAG,
    ) {
        Text(
            text = taffyString(R.string.taffy_report_answer_body),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
        )
        Text(
            text = taffyString(R.string.taffy_report_answer_email_explainer),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffyPrimaryButton(
            label = taffyString(R.string.taffy_report_answer_email),
            onClick = {
                onIntent(ReportAnswerIntent.EmailDraft(openEmailDraft(context, subject, body)))
            },
            testTag = REPORT_ANSWER_EMAIL_TEST_TAG,
        )
        if (state.emailUnavailable) {
            SelectionContainer(modifier = Modifier.testTag(REPORT_ANSWER_NO_EMAIL_TEST_TAG)) {
                Text(
                    text = taffyString(
                        R.string.taffy_report_answer_no_email_app,
                        TaffyProjectContact.EMAIL,
                    ),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textPrimary,
                )
            }
        }
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_report_answer_issue),
            onClick = { onIntent(ReportAnswerIntent.OpenPublicIssue(issueTitle)) },
            testTag = REPORT_ANSWER_ISSUE_TEST_TAG,
        )
        Text(
            text = taffyString(R.string.taffy_report_answer_issue_public),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/** The tags the report sheet's semantics tests name. */
const val REPORT_ANSWER_BUTTON_TEST_TAG: String = "report_answer_button"
const val REPORT_ANSWER_SHEET_TEST_TAG: String = "report_answer_sheet"
const val REPORT_ANSWER_EMAIL_TEST_TAG: String = "report_answer_email"
const val REPORT_ANSWER_ISSUE_TEST_TAG: String = "report_answer_issue"
const val REPORT_ANSWER_NO_EMAIL_TEST_TAG: String = "report_answer_no_email"

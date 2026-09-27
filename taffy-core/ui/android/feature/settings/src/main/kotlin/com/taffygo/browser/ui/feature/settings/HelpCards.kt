// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.TaffyProjectContact
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.installedVersionName
import com.taffygo.browser.ui.core.ui.openEmailDraft
import com.taffygo.browser.ui.core.ui.taffyString

/** One honest limit. Not a destination. */
@Composable
internal fun HelpLimitCard(
    title: String,
    body: String,
    icon: ImageVector,
    testTag: String,
) {
    TaffyObjectCard(testTag = testTag) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SettingsGlyph(icon)
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            ) {
                Text(
                    text = title,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                )
                Text(
                    text = body,
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
    }
}

/**
 * The two ways to reach the people who make TaffyGo, and what each one does.
 *
 * Email hands a draft to the person's own email app, which is the whole of
 * what this screen can do with it; the app sends it or the person throws it
 * away. The issue opens GitHub in a new tab, and the line under it says the
 * issue is public before anybody writes one.
 */
@Composable
internal fun HelpReachCard(
    emailUnavailable: Boolean,
    onIntent: (HelpIntent) -> Unit,
) {
    val context = LocalContext.current
    val subject = taffyString(R.string.taffy_help_feedback_email_subject)
    val body = taffyString(
        R.string.taffy_help_feedback_email_body,
        installedVersionName(context) ?: taffyString(R.string.taffy_help_feedback_version_unknown),
    )
    TaffyObjectCard(testTag = HELP_REACH_TEST_TAG) {
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            Text(
                text = taffyString(R.string.taffy_help_feedback_body),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_help_feedback_email),
                onClick = {
                    onIntent(HelpIntent.FeedbackByEmail(openEmailDraft(context, subject, body)))
                },
                testTag = HELP_FEEDBACK_EMAIL_TEST_TAG,
            )
            if (emailUnavailable) {
                SelectionContainer(modifier = Modifier.testTag(HELP_FEEDBACK_NO_EMAIL_TEST_TAG)) {
                    Text(
                        text = taffyString(
                            R.string.taffy_help_feedback_no_email_app,
                            TaffyProjectContact.EMAIL,
                        ),
                        style = TaffyTheme.typography.detail,
                        color = TaffyTheme.colors.textPrimary,
                    )
                }
            }
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_help_feedback_issue),
                onClick = { onIntent(HelpIntent.OpenPublicIssue) },
                testTag = HELP_FEEDBACK_ISSUE_TEST_TAG,
            )
            Text(
                text = taffyString(R.string.taffy_help_feedback_issue_public),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

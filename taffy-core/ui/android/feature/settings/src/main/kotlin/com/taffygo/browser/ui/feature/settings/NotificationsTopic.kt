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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.TaffySwitch
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * One notification topic. The card is inert; only [TaffySwitch] is pressable,
 * matching OpenAlly's SettingsToggleItem.
 */
@Composable
internal fun NotificationTopicCard(
    topic: NotificationTopic,
    checked: Boolean,
    enabled: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    val name = taffyString(topicName(topic))
    TaffyObjectCard(testTag = "$TOPIC_TEST_TAG_PREFIX${topic.label}") {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SettingsGlyph(topicIcon(topic))
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            ) {
                Text(
                    text = name,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                )
                Text(
                    text = taffyString(topicSummary(topic)),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
            TaffySwitch(
                checked = checked,
                onCheckedChange = onCheckedChange,
                enabled = enabled,
                accessibleName = name,
                testTag = "$SWITCH_TEST_TAG_PREFIX${topic.label}",
            )
        }
    }
}

private fun topicName(topic: NotificationTopic) = when (topic) {
    NotificationTopic.TASK_PROGRESS -> R.string.taffy_notifications_task_progress
    NotificationTopic.DOWNLOADS -> R.string.taffy_notifications_downloads
}

private fun topicSummary(topic: NotificationTopic) = when (topic) {
    NotificationTopic.TASK_PROGRESS -> R.string.taffy_notifications_task_progress_summary
    NotificationTopic.DOWNLOADS -> R.string.taffy_notifications_downloads_summary
}

private fun topicIcon(topic: NotificationTopic): ImageVector = when (topic) {
    NotificationTopic.TASK_PROGRESS -> TaffyIcon.BellRinging
    NotificationTopic.DOWNLOADS -> TaffyIcon.DownloadSimple
}

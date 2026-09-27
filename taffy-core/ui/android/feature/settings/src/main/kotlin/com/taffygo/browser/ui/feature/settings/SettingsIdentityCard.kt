// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.ui.TaffyProfileAvatar
import com.taffygo.browser.ui.core.ui.TaffyCharacterBust
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.TaffyPressable
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The You door on screen SCR-401.
 *
 * Two facts and no third one: the picture this phone's profile wears and the
 * name it holds. Neither is asked of a server, so there is no resolving
 * state to draw and no standing to draw beside them — see decision 0201.
 */
@Composable
fun SettingsIdentityCard(
    displayName: String?,
    avatar: LocalAvatar,
    monogram: String,
    onOpenYou: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val name = displayName?.trim()?.takeIf { it.isNotEmpty() }
    val title = name ?: taffyString(R.string.taffy_settings_you_title)
    val spoken = taffyString(R.string.taffy_settings_you_opens, title)
    TaffyObjectCard(modifier = modifier) {
        TaffyPressable(
            onClick = onOpenYou,
            modifier = Modifier
                .fillMaxWidth()
                .semantics(mergeDescendants = true) { contentDescription = spoken },
            testTag = SETTINGS_HEADER_TEST_TAG,
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                TaffyProfileAvatar(
                    avatar = avatar,
                    monogram = monogram,
                    size = TaffyCharacterBust,
                )
                Text(
                    text = title,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f),
                )
                Icon(
                    imageVector = TaffyIcon.CaretRight,
                    contentDescription = null,
                    tint = TaffyTheme.colors.hairline,
                    modifier = Modifier.size(ChevronSize),
                )
            }
        }
    }
}

private val ChevronSize = 18.dp

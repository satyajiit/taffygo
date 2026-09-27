// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.ui.TaffyHeroCard
import com.taffygo.browser.ui.core.ui.TaffyPressable
import com.taffygo.browser.ui.core.ui.taffyString

/** You hub hero: destination canvas, trailing clay still, and one name. */
@Composable
internal fun YouHero(
    state: YouUiState,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val title = youHeroTitle(state)
    val body = taffyString(R.string.taffy_you_hero_body)
    val spoken = taffyString(R.string.taffy_you_header_description, title, body)
    TaffyPressable(
        onClick = onClick,
        modifier = modifier.fillMaxWidth(),
        testTag = YOU_HEADER_TEST_TAG,
    ) {
        TaffyHeroCard(
            title = title,
            eyebrow = taffyString(R.string.taffy_settings_you_title),
            body = body,
            illustration = painterResource(R.drawable.taffy_hero_you),
            illustrationSize = YouHeroIllustration,
            modifier = Modifier.semantics(mergeDescendants = true) {
                contentDescription = spoken
                role = Role.Button
            },
        )
    }
}

/**
 * The hub's own title: this phone's name for its owner, and the screen's
 * name when there is none. Never a placeholder standing in for an identity
 * the phone does not have.
 */
@Composable
internal fun youHeroTitle(state: YouUiState): String =
    youShownName(state) ?: taffyString(R.string.taffy_settings_you_title)

private val YouHeroIllustration = 192.dp

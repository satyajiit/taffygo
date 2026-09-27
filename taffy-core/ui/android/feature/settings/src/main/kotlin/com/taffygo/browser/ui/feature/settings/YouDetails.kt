// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffyPressable
import com.taffygo.browser.ui.core.ui.TaffyProfileAvatar
import com.taffygo.browser.ui.core.ui.taffyString

/** Profile details on You: circular picture, the name, and the picture grid. */
@Composable
internal fun YouDetails(
    state: YouUiState,
    onIntent: (YouIntent) -> Unit,
) {
    YouDetailsIdentity(state = state)
    YouNameField(
        value = youNameField(state),
        onChange = { onIntent(YouIntent.EditName(it)) },
        onDone = { onIntent(YouIntent.CommitName) },
    )
    YouAvatarPicker(
        selected = state.avatar,
        monogram = state.monogram,
        onChoose = { onIntent(YouIntent.ChooseAvatar(it)) },
    )
    TaffyInfoTile {
        Text(
            text = taffyString(R.string.taffy_you_profile_note),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

@Composable
private fun YouDetailsIdentity(state: YouUiState) {
    val name = youShownName(state)
        ?: taffyString(R.string.taffy_you_name_empty)
    TaffyGroupedCard(testTag = YOU_DETAILS_SLAB_TEST_TAG) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(TaffyTheme.spacing.screenMargin),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            TaffyProfileAvatar(
                avatar = state.avatar,
                monogram = state.monogram,
                size = IdentityPicture,
                shape = CircleShape,
            )
            Text(
                text = name,
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
            )
        }
    }
}

/**
 * The one field this screen writes.
 *
 * It holds what was typed and stores it on Done, rather than writing each
 * keystroke: the store trims and bounds what it is given, so a value echoed
 * straight back would swallow the space between a first and a last name.
 */
@Composable
private fun YouNameField(
    value: String,
    onChange: (String) -> Unit,
    onDone: () -> Unit,
) {
    OutlinedTextField(
        value = value,
        onValueChange = onChange,
        label = { Text(taffyString(R.string.taffy_you_name_label)) },
        placeholder = { Text(taffyString(R.string.taffy_you_name_placeholder)) },
        singleLine = true,
        keyboardOptions = KeyboardOptions(
            capitalization = KeyboardCapitalization.Words,
            keyboardType = KeyboardType.Text,
            imeAction = ImeAction.Done,
        ),
        keyboardActions = KeyboardActions(onDone = { onDone() }),
        shape = TaffyTheme.shapes.card,
        colors = OutlinedTextFieldDefaults.colors(
            focusedContainerColor = TaffyTheme.colors.surfaceRaised,
            unfocusedContainerColor = TaffyTheme.colors.surfaceRaised,
        ),
        modifier = Modifier
            .fillMaxWidth()
            .testTag(YOU_NAME_TEST_TAG),
    )
}

@Composable
private fun YouAvatarPicker(
    selected: LocalAvatar,
    monogram: String,
    onChoose: (LocalAvatar) -> Unit,
) {
    /*
     * The monogram leads the strip rather than sitting at the end as a way
     * out. It is the face a profile already has, so offering it last would
     * read as "none of these", which is a different answer from the one it
     * actually is.
     */
    val choices: List<LocalAvatar> =
        listOf(LocalAvatar.Monogram) + LocalAvatar.TILE_IDS.map(LocalAvatar::of)
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        SettingsHomeEyebrow(title = taffyString(R.string.taffy_you_banner_label))
        if (choices.isEmpty()) {
            TaffyInfoTile(testTag = YOU_BANNER_LIST_TEST_TAG) {
                Text(
                    text = taffyString(R.string.taffy_you_banner_unavailable),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        } else {
            TaffyGroupedCard(testTag = YOU_BANNER_LIST_TEST_TAG) {
                Column(
                    modifier = Modifier.padding(
                        horizontal = TaffyTheme.spacing.screenMargin,
                        vertical = TaffyTheme.spacing.snug,
                    ),
                    verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                ) {
                    choices.chunked(BannerColumns).forEachIndexed { rowIndex, rowItems ->
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                        ) {
                            rowItems.forEachIndexed { columnIndex, choice ->
                                val index = rowIndex * BannerColumns + columnIndex
                                val description = taffyString(
                                    R.string.taffy_you_banner_choice,
                                    index + 1,
                                    choices.size,
                                )
                                TaffyPressable(
                                    onClick = { onChoose(choice) },
                                    modifier = Modifier
                                        .weight(1f)
                                        .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
                                        .semantics { contentDescription = description },
                                    testTag =
                                        "$YOU_BANNER_TEST_TAG_PREFIX${youAvatarTag(choice)}",
                                ) {
                                    Box(
                                        modifier = Modifier.fillMaxWidth(),
                                        contentAlignment = Alignment.Center,
                                    ) {
                                        TaffyProfileAvatar(
                                            avatar = choice,
                                            monogram = monogram,
                                            size = BannerTile,
                                            selected = choice == selected,
                                            shape = CircleShape,
                                        )
                                    }
                                }
                            }
                            repeat(BannerColumns - rowItems.size) {
                                Spacer(modifier = Modifier.weight(1f))
                            }
                        }
                    }
                }
            }
        }
    }
}

private const val BannerColumns = 4
private val BannerTile = 56.dp
private val IdentityPicture = 96.dp
const val YOU_DETAILS_SLAB_TEST_TAG: String = "you_details_slab"
const val YOU_BANNER_LIST_TEST_TAG: String = "you_banner_list"
const val YOU_BANNER_TEST_TAG_PREFIX: String = "you_banner_"
const val YOU_NAME_TEST_TAG: String = "you_name"

/**
 * What a choice is called in a test tag.
 *
 * The monogram has no id — that is the whole of what distinguishes it from a
 * tile — so it needs a name of its own here rather than an empty string, which
 * would make its tag indistinguishable from a missing one.
 */
internal fun youAvatarTag(avatar: LocalAvatar): String = when (avatar) {
    LocalAvatar.Monogram -> "monogram"
    is LocalAvatar.Tile -> avatar.id
}

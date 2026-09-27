// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyProfileAvatar
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The one card on SCR-007: a face, a name, and thirty-one ways to change the
 * face.
 *
 * Both answers are optional and neither leaves the phone, so nothing here is
 * validated, refused or marked required. The only rule it enforces is the
 * length bound, and it enforces it by not accepting the next letter.
 */
@Composable
internal fun GetStartedProfileCard(
    state: GetStartedUiState,
    onIntent: (GetStartedIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    TaffyGroupedCard(modifier = modifier, testTag = GET_STARTED_CARD_TEST_TAG) {
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
                size = ChosenFace,
                shape = CircleShape,
                modifier = Modifier.testTag(GET_STARTED_FACE_TEST_TAG),
            )
            OutlinedTextField(
                value = state.name,
                onValueChange = { onIntent(GetStartedIntent.EditName(it)) },
                label = { Text(taffyString(R.string.taffy_get_started_name_label)) },
                placeholder = { Text(taffyString(R.string.taffy_get_started_name_placeholder)) },
                singleLine = true,
                keyboardOptions = KeyboardOptions(
                    capitalization = KeyboardCapitalization.Words,
                    keyboardType = KeyboardType.Text,
                    imeAction = ImeAction.Done,
                ),
                shape = TaffyTheme.shapes.card,
                colors = OutlinedTextFieldDefaults.colors(
                    focusedContainerColor = TaffyTheme.colors.surfaceRaised,
                    unfocusedContainerColor = TaffyTheme.colors.surfaceRaised,
                ),
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag(GET_STARTED_NAME_TEST_TAG),
            )
            GetStartedFaceStrip(
                selected = state.avatar,
                monogram = state.monogram,
                onChoose = { onIntent(GetStartedIntent.ChooseAvatar(it)) },
            )
        }
    }
}

/**
 * The faces, as one horizontal run.
 *
 * The monogram leads rather than trailing. It is the face the profile already
 * wears, so putting it last would read as "none of these" — a different
 * answer from the one it is.
 */
@Composable
private fun GetStartedFaceStrip(
    selected: LocalAvatar,
    monogram: String,
    onChoose: (LocalAvatar) -> Unit,
) {
    val choices: List<LocalAvatar> =
        listOf(LocalAvatar.Monogram) + LocalAvatar.TILE_IDS.map(LocalAvatar::of)
    val groupLabel = taffyString(R.string.taffy_get_started_face_label)
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = groupLabel,
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.fillMaxWidth(),
        )
        LazyRow(
            modifier = Modifier
                .fillMaxWidth()
                .selectableGroup()
                .testTag(GET_STARTED_FACES_TEST_TAG),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            contentPadding = PaddingValues(vertical = TaffyTheme.spacing.step),
        ) {
            itemsIndexed(choices, key = { _, choice -> getStartedFaceTag(choice) }) {
                index, choice ->
                val description = taffyString(
                    R.string.taffy_get_started_face_choice,
                    index + 1,
                    choices.size,
                )
                Box(
                    modifier = Modifier
                        .size(FaceTarget)
                        .selectable(
                            selected = choice == selected,
                            role = Role.RadioButton,
                            onClick = { onChoose(choice) },
                        )
                        .semantics { contentDescription = description }
                        .testTag("$GET_STARTED_FACE_TEST_TAG_PREFIX${getStartedFaceTag(choice)}"),
                    contentAlignment = Alignment.Center,
                ) {
                    TaffyProfileAvatar(
                        avatar = choice,
                        monogram = monogram,
                        size = FaceTile,
                        selected = choice == selected,
                        shape = CircleShape,
                    )
                }
            }
        }
    }
}

/**
 * What a choice is called in a test tag and as a list key.
 *
 * The monogram has no id — that is the whole of what distinguishes it from a
 * tile — so it needs a name of its own rather than an empty string, which
 * would be indistinguishable from a missing one and would collide as a key.
 */
internal fun getStartedFaceTag(avatar: LocalAvatar): String = when (avatar) {
    LocalAvatar.Monogram -> "monogram"
    is LocalAvatar.Tile -> avatar.id
}

/** The face a person is building, shown at the size the mock draws it. */
private val ChosenFace = 96.dp

/** The tile, inside a touch target that meets the minimum on its own. */
private val FaceTile = 44.dp
private val FaceTarget = 48.dp

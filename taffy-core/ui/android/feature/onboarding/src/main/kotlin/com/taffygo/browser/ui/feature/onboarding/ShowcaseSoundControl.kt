// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import android.content.Context
import android.media.AudioManager
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/** The first local sound state follows the device without changing it. */
internal fun defaultShowcaseSoundEnabled(context: Context): Boolean {
    val audioManager = context.getSystemService(AudioManager::class.java) ?: return false
    return showcaseSoundEnabledForRingerMode(audioManager.ringerMode)
}

internal fun showcaseSoundEnabledForRingerMode(ringerMode: Int): Boolean =
    ringerMode == AudioManager.RINGER_MODE_NORMAL

@Composable
internal fun rememberShowcaseSoundEnabled(
    initialEnabled: () -> Boolean,
): MutableState<Boolean> = rememberSaveable { mutableStateOf(initialEnabled()) }

/**
 * A forty-eight-unit sound target around a well that reads at the lockup's own
 * weight.
 *
 * ## Why this is not a `TaffyIconButton`
 *
 * It was one, at the default size and at `PRIMARY` while sound was on, and
 * that is a solid `textPrimary` circle forty-eight units across with a
 * twenty-one unit glyph. Three things were wrong with it at once. `PRIMARY` is
 * defined as the one action a screen most wants taken, and on this screen that
 * is Continue — so the screen carried two ink fills and the smaller one was
 * louder per unit than the twenty-eight unit brand lockup beside it. The glyph
 * was at the size `handoff/DESIGN.md` section 4 reserves for the action row
 * rather than the fourteen-to-seventeen it gives a pill. And the tint ran the
 * wrong way round: section 4 asks for `textSecondary` at rest and
 * `textPrimary` when live, and a filled button inverts both.
 *
 * So this follows the treatment the product already uses for a control that
 * sits beside content rather than under a thumb: the tab switcher's close
 * control and the welcome screen's language chip both wrap a small outlined
 * well in a full-size invisible target. `docs/design/screen-catalog.md` fixes
 * the target at forty-eight units and says nothing about the well, which is
 * the part that was too big.
 */
@Composable
internal fun ShowcaseSoundButton(
    soundEnabled: Boolean,
    onSoundEnabledChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    val label = if (soundEnabled) {
        taffyString(R.string.taffy_showcase_sound_turn_off)
    } else {
        taffyString(R.string.taffy_showcase_sound_turn_on)
    }
    val currentState = if (soundEnabled) {
        taffyString(R.string.taffy_showcase_sound_state_on)
    } else {
        taffyString(R.string.taffy_showcase_sound_state_off)
    }
    Box(
        modifier = modifier
            .size(TaffyTheme.spacing.minimumTouchTarget)
            .clickable(role = Role.Button) { onSoundEnabledChange(!soundEnabled) }
            .testTag(SHOWCASE_SOUND_TEST_TAG)
            .semantics(mergeDescendants = true) {
                contentDescription = label
                stateDescription = currentState
            },
        contentAlignment = Alignment.Center,
    ) {
        Box(
            modifier = Modifier
                .size(SoundWellSize)
                .clip(TaffyTheme.shapes.pill)
                .background(TaffyTheme.colors.surfaceRaised)
                .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.pill),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                imageVector = if (soundEnabled) TaffyIcon.SpeakerHigh else TaffyIcon.SpeakerSlash,
                contentDescription = null,
                tint = if (soundEnabled) {
                    TaffyTheme.colors.textPrimary
                } else {
                    TaffyTheme.colors.textSecondary
                },
                modifier = Modifier.size(SoundGlyphSize),
            )
        }
    }
}

internal const val SHOWCASE_SOUND_TEST_TAG: String = "onboarding_showcase_sound"

/** The visible well: one step up from the tab switcher's close control, so it
 *  sits beside a twenty-eight unit lockup rather than over it. */
private val SoundWellSize = 32.dp

/** `handoff/DESIGN.md` section 4: fourteen to seventeen for a glyph in a pill. */
private val SoundGlyphSize = 16.dp

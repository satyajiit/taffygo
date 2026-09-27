// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.drawTaffyBorderComet
import com.taffygo.browser.ui.core.designsystem.markRibbon
import com.taffygo.browser.ui.core.designsystem.taffyPhase
import com.taffygo.browser.ui.core.designsystem.taffySmootherStep
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.VoiceEntryState
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The one box, open: the options, the words, Speak, and send.
 *
 * This is the box screen SCR-102 and an empty tab on SCR-101 centre, and it is
 * the box screen SCR-103 draws too — one composable, so the four readings have
 * one field to be got wrong in. Decision 0050 kept the start page's box closed
 * for exactly that reason; typing here honours the reason by sharing the
 * implementation rather than by growing a second one.
 *
 * Four controls, and each earns its place. The plus at the leading edge carries
 * the options and the destinations that used to need a screen change to reach.
 * The words are in the middle. Speak and send sit inside the field's own edge at
 * the trailing end, which is the shape [AskComposerField][
 * com.taffygo.browser.ui.feature.assistant] already gives the product's other
 * composer — two composers that look alike because they are alike, rather than
 * two that drift.
 *
 * **Send is the orb.** While there is nothing to send, the disc is the dissolving
 * glyph decision 0050 section 6 put at the end of the closed box — search, a
 * site, a question, a task — so the box still says what it can be. The moment
 * the words mean something the same disc becomes the send arrow. Nothing is
 * added and nothing is lost; one control changes its face.
 */
@Composable
internal fun StartPageComposer(
    state: AddressBarUiState,
    onIntent: (AddressBarIntent) -> Unit,
    modifier: Modifier = Modifier,
    rowTestTag: String,
    placeholder: String,
    onFocusChanged: (Boolean) -> Unit = {},
    menu: (@Composable () -> Unit)? = null,
    showOptions: Boolean = true,
) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        ComposerField(
            state = state,
            onIntent = onIntent,
            onFocusChanged = onFocusChanged,
            rowTestTag = rowTestTag,
            placeholder = placeholder,
            menu = menu,
        )
        if (showOptions) StartPageOptionChips(state = state, onIntent = onIntent)
    }
}

@Composable
private fun ComposerField(
    state: AddressBarUiState,
    onIntent: (AddressBarIntent) -> Unit,
    onFocusChanged: (Boolean) -> Unit,
    rowTestTag: String,
    placeholder: String,
    menu: (@Composable () -> Unit)?,
) {
    val keyboard = LocalSoftwareKeyboardController.current
    val colors = TaffyTheme.colors
    val still = TaffyTheme.reducedMotion
    val asksInPlace = state.conditions.asksInPlace
    val phase = taffyPhase(running = !still && !asksInPlace, periodMillis = CometPeriodMillis)
    // The mark's own ramp rather than one accent stroke: the box a person types
    // their request into is the product introducing itself, and the light that
    // travels its edge is the only moving thing on the start page.
    val ribbon = colors.markRibbon
    val reading = state.reading
    val shape = if (asksInPlace) TaffyTheme.shapes.card else TaffyTheme.shapes.pill
    val send: () -> Unit = {
        if (reading != null) {
            keyboard?.hide()
            onIntent(AddressBarIntent.Choose(reading))
        }
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = ComposerHeight)
            .clip(shape)
            .background(if (asksInPlace) colors.surfaceSunken else colors.surfaceRaised)
            .border(TaffyBorders.standard, colors.outline, shape)
            .drawBehind { if (!still && !asksInPlace) drawTaffyBorderComet(phase.value, ribbon) }
            .padding(
                horizontal = TaffyTheme.spacing.step,
                vertical = if (asksInPlace) TaffyTheme.spacing.tight else 0.dp,
            )
            .testTag(rowTestTag),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // The plus is drawn only where there is a menu behind it. Screen
        // SCR-103 has a frame, a back arrow and its own chips; the start page
        // has no chrome at all, which is the whole reason the options live on
        // the box there.
        if (menu != null) {
            Box {
                ComposerGlyph(
                    icon = TaffyIcon.Plus,
                    contentDescription = taffyString(R.string.taffy_start_options),
                    enabled = true,
                    onClick = { onIntent(AddressBarIntent.OpenMenu) },
                    testTag = START_OPTIONS_TEST_TAG,
                )
                if (state.menuOpen) menu()
            }
        }
        ComposerWords(
            state = state,
            onIntent = onIntent,
            onFocusChanged = onFocusChanged,
            onSend = send,
            placeholder = placeholder,
            modifier = Modifier.weight(1f),
        )
        ComposerGlyph(
            icon = TaffyIcon.Waveform,
            contentDescription = taffyString(R.string.taffy_address_voice_start),
            enabled = state.voiceEntry == VoiceEntryState.Closed,
            onClick = { onIntent(AddressBarIntent.StartVoiceInput) },
            testTag = ADDRESS_VOICE_START_TEST_TAG,
        )
        ComposerDisc(reading = reading, onSend = send)
    }
}

@Composable
private fun ComposerWords(
    state: AddressBarUiState,
    onIntent: (AddressBarIntent) -> Unit,
    onFocusChanged: (Boolean) -> Unit,
    onSend: () -> Unit,
    placeholder: String,
    modifier: Modifier = Modifier,
) {
    val colors = TaffyTheme.colors
    // The field reports focus and does not interpret it. One host answers by
    // opening a tab and both answer by folding the welcome away, and neither is
    // a decision a text field should be making.
    val focused = remember { mutableStateOf(false) }
    BasicTextField(
        value = state.input,
        onValueChange = { onIntent(AddressBarIntent.InputChanged(it)) },
        singleLine = !state.conditions.asksInPlace,
        maxLines = if (state.conditions.asksInPlace) 4 else 1,
        textStyle = TaffyTheme.typography.body.copy(color = colors.textPrimary),
        cursorBrush = SolidColor(colors.accent),
        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Go),
        // The key on the keyboard and the disc are one control, carrying the
        // reading the box is already showing — so the key can no more start
        // something unannounced than the disc can.
        keyboardActions = KeyboardActions { onSend() },
        modifier = modifier
            .onFocusChanged { focus ->
                if (focus.isFocused != focused.value) {
                    focused.value = focus.isFocused
                    onFocusChanged(focus.isFocused)
                }
            }
            .testTag(ADDRESS_INPUT_TEST_TAG),
        decorationBox = { field ->
            Box(contentAlignment = Alignment.CenterStart) {
                if (state.input.isEmpty()) {
                    Text(
                        text = placeholder,
                        style = TaffyTheme.typography.body,
                        color = colors.textSecondary,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
                field()
            }
        },
    )
}

/** A bare glyph in a full touch target: a control inside the field's own edge. */
@Composable
private fun ComposerGlyph(
    icon: ImageVector,
    contentDescription: String,
    enabled: Boolean,
    onClick: () -> Unit,
    testTag: String,
) {
    val colors = TaffyTheme.colors
    Box(
        modifier = Modifier
            .size(GlyphTarget)
            .clip(CircleShape)
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick)
            .semantics { this.contentDescription = contentDescription }
            .testTag(testTag),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            tint = if (enabled) {
                colors.textSecondary
            } else {
                colors.textPrimary.copy(alpha = DisabledAlpha)
            },
            modifier = Modifier.size(GlyphSize),
        )
    }
}

/**
 * The ink disc at the end of the box: what it can be, then what it will do.
 *
 * With no reading it is decision 0050's dissolving orb and is not a control at
 * all — there is nothing to send, and a disabled arrow would be a control that
 * looks like it can do its job. With a reading it is the send key the whole
 * product uses.
 */
@Composable
private fun ComposerDisc(
    reading: AddressBarInterpretation?,
    onSend: () -> Unit,
) {
    val colors = TaffyTheme.colors
    val sends = reading != null
    // The reading and the words, which is what the line under the box already
    // says: a disc named only "Go" would be a control whose spoken name is the
    // same whether it is about to open a site or start a task.
    val description = if (reading == null) {
        ""
    } else {
        taffyString(
            R.string.taffy_address_bar_send_description,
            taffyString(readingLabel(reading)),
            reading.input,
        )
    }
    Box(
        modifier = Modifier
            .size(GlyphTarget)
            .clip(CircleShape)
            .then(
                if (sends) {
                    Modifier
                        .clickable(role = Role.Button, onClick = onSend)
                        .semantics { contentDescription = description }
                        .testTag(START_SEND_TEST_TAG)
                } else {
                    Modifier
                },
            ),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            modifier = Modifier
                .size(DiscSize)
                .clip(CircleShape)
                .background(colors.textPrimary),
            contentAlignment = Alignment.Center,
        ) {
            if (sends) {
                Icon(
                    imageVector = TaffyIcon.ArrowUp,
                    contentDescription = null,
                    tint = colors.surface,
                    modifier = Modifier.size(DiscGlyphSize),
                )
            } else {
                ComposerOrb()
            }
        }
    }
}

/**
 * The four readings the box can open, one glyph at a time.
 *
 * Search, a site, a question, a task. The dissolve is the greeting's own
 * envelope on [taffyPhase], so reduced motion holds the first glyph still and
 * the swap never lays two icons on top of each other.
 */
@Composable
private fun ComposerOrb() {
    val still = TaffyTheme.reducedMotion
    val phase by taffyPhase(
        running = !still,
        periodMillis = OrbCycleMillis * EntryGlyphs.size,
    )
    val position = phase * EntryGlyphs.size
    val index = position.toInt().coerceAtMost(EntryGlyphs.size - 1)
    val within = position - index
    val next = (index + 1) % EntryGlyphs.size
    // Crossfade rather than a hole: a dissolve that bottoms out at zero leaves
    // the disc blank for a beat. The arriving glyph is already rising as the
    // leaving one fades.
    val leaving: Float
    val arriving: Float
    when {
        still -> {
            leaving = 1f
            arriving = 0f
        }
        within > 1f - OrbFade -> {
            val t = taffySmootherStep((within - (1f - OrbFade)) / OrbFade)
            leaving = 1f - t
            arriving = t
        }
        else -> {
            leaving = 1f
            arriving = 0f
        }
    }
    Box(contentAlignment = Alignment.Center) {
        OrbGlyph(imageVector = EntryGlyphs[index], visibility = leaving)
        if (arriving > 0f) {
            OrbGlyph(imageVector = EntryGlyphs[next], visibility = arriving)
        }
    }
}

@Composable
private fun OrbGlyph(imageVector: ImageVector, visibility: Float) {
    Icon(
        imageVector = imageVector,
        contentDescription = null,
        modifier = Modifier
            .size(DiscGlyphSize)
            .graphicsLayer {
                alpha = visibility
                val scale = 0.88f + 0.12f * visibility
                scaleX = scale
                scaleY = scale
            },
        tint = TaffyTheme.colors.surface,
    )
}

/** The plus, which the start page's semantics tests name. */
const val START_OPTIONS_TEST_TAG: String = "start_options"

/** The send disc, a control only once the words mean something. */
const val START_SEND_TEST_TAG: String = "start_send"

// The box outgrows the 44-unit address pill on purpose — it is the page's
// centre, not one control among several — and its controls are the assistant
// composer's own sizes so the two read as one family.
private val ComposerHeight = 56.dp
private val GlyphTarget = 44.dp
private val GlyphSize = 20.dp
private val DiscSize = 36.dp
private val DiscGlyphSize = 18.dp
private const val DisabledAlpha = 0.38f

// Search, a site, a question, a task — the four readings, in the order a person
// meets them. Each glyph holds long enough to be read; the dissolve is the
// greeting's own fraction of the slice.
private val EntryGlyphs = listOf(
    TaffyIcon.MagnifyingGlass,
    TaffyIcon.GlobeSimple,
    TaffyIcon.ChatCircle,
    TaffyIcon.Sparkle,
)
private const val OrbCycleMillis = 2_800
private const val OrbFade = 0.18f

// One lap takes long enough to read as weather rather than as progress.
private const val CometPeriodMillis = 3200

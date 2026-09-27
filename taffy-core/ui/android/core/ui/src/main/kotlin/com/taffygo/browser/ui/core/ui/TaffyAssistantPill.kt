// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.drawTaffyBorderComet
import com.taffygo.browser.ui.core.designsystem.markRibbon
import com.taffygo.browser.ui.core.designsystem.taffyPhase
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaskControl

/**
 * The assistant pill (`handoff/TgAssistantPill.dc.html`): one composable, one
 * line, one control per state — at one height, in every state there is.
 *
 * Idle is the invitation, "Ask Taffy" and the ink orb. Running is a status line
 * under a light travelling round the pill's own outline. Waiting is the only
 * solid accent in the UI, because it is the only state that needs a person, and
 * its chip is Review. Paused is an outline pill with Resume. Running carries no
 * chip of its own: pause is not a thing anybody wants of a task that is working,
 * and the control that matters while Taffy drives is take over.
 * The four final states wear the design document's own task-state
 * treatments, read from the same table the status chip reads
 * ([taffyStatusSkin]), and carry no chip at all: the whole pill is the way to
 * what happened, which is what "one line, one control" means for a task that
 * has stopped needing controlling.
 *
 * **Take over is the second chip, on every state a person can still steer.** It
 * is the one control here that is about them rather than about the task's
 * machinery, and it is on the pill because decision 0141 removed the band above
 * the row that used to carry it — a second surface saying what this line says,
 * with the one control that gives the page back stranded on it. The caller
 * decides whether it exists, from the reducer's own control list.
 *
 * **The line travels rather than truncating** ([TaffyMarqueeText]). The row this
 * sits in leaves it about 250 dp while a task is under way and 164 when the
 * controls are back, and the sentences the core sends are longer than either;
 * an ellipsis on the one line that says what is happening is the wrong failure.
 *
 * **The height is the invariant.** Every state is [PillHeight], because the
 * browser draws this into an action row of a fixed height and a state that
 * wanted more was clipped there without anything failing.
 *
 * **Work is shown by the outline, not by a bar.** The handoff draws a rail that
 * grows across the pill's bottom edge over nine seconds, and on a phone a bar
 * that fills is a promise about how far along the task is that nothing here can
 * keep — a task has no such number. The travelling light
 * ([drawTaffyBorderComet]) is the start page's own composer effect, and it says
 * the one true thing: something is working. It is the same motion in the same
 * colours on both surfaces — `TaffyColors.markRibbon`, the mark's own ramp,
 * so the moving thing is the brand rather than a status hue — which is what
 * makes the bar read as Taffy's rather than as a progress bar's.
 *
 * The component is stateless: the caller owns the status line, the three
 * callbacks, and [stateWord].
 *
 * [labelModifier] lands on the one visible label — the idle invitation or the
 * status line — so a screen can tag it or give it its own words for a screen
 * reader without forking the pill. [stateWord] is the state's own word,
 * announced ahead of the line; the caller owns it because only the caller knows
 * whether the word is still true, and a task nothing is driving is still
 * "Running" to the task machine after the line has said it is not.
 */
@Composable
fun TaffyAssistantPill(
    state: TaffyAssistantPillState,
    modifier: Modifier = Modifier,
    label: String? = null,
    onClick: (() -> Unit)? = null,
    onAction: (() -> Unit)? = null,
    onTakeOver: (() -> Unit)? = null,
    labelModifier: Modifier = Modifier,
    stateWord: String? = null,
) {
    val colors = TaffyTheme.colors
    val reducedMotion = TaffyTheme.reducedMotion
    val face = pillFace(state)
    val askTaffy = taffyString(R.string.taffy_assistant_ask_taffy)
    val visibleLabel = if (state == TaffyAssistantPillState.IDLE) askTaffy else label.orEmpty()
    // Take over is the one control that is not about the task's own machinery,
    // and it is on the pill because there is nowhere else in the chrome for it
    // to be: the band that used to carry it was a second surface above the row
    // saying what the pill already says (decision 0141).
    val showsTakeOver = onTakeOver != null && face.takesControls
    val paddingEnd = if (showsTakeOver) PillPaddingEnd else face.paddingEnd
    // The travelling light, and only while it is actually travelling: under
    // reduced motion nothing is drawn and no frame is scheduled.
    val cometRuns = face.showsComet && !reducedMotion
    val cometPhase = taffyPhase(running = cometRuns, periodMillis = CometPeriodMillis)
    val cometRibbon = colors.markRibbon
    Box(
        modifier = modifier
            .height(PillHeight)
            .clip(TaffyTheme.shapes.pill)
            .background(face.background)
            .then(
                if (face.border != null) {
                    Modifier.border(TaffyBorders.standard, face.border, TaffyTheme.shapes.pill)
                } else {
                    Modifier
                },
            )
            .drawBehind {
                if (!cometRuns) return@drawBehind
                drawTaffyBorderComet(
                    phase = cometPhase.value,
                    ribbon = cometRibbon,
                    cornerRadius = CornerRadius(size.height / 2f),
                )
            }
            .then(
                // Every state opens the assistant, not only idle. A finished
                // task carries no chip, so the pill itself has to be the door
                // to its results.
                if (onClick != null) {
                    Modifier.clickable(onClick = onClick, role = Role.Button)
                } else {
                    Modifier
                },
            )
            .semantics {
                liveRegion = LiveRegionMode.Polite
                stateDescription = stateWord ?: visibleLabel
            },
    ) {
        Row(
            modifier = Modifier
                .fillMaxHeight()
                .padding(start = PillPaddingStart, end = paddingEnd),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (face.glyph != null) {
                Icon(
                    imageVector = face.glyph,
                    // The shape repeats what the line already says; announcing
                    // it twice would be noise.
                    contentDescription = null,
                    modifier = Modifier.size(face.glyphSize),
                    tint = face.ink,
                )
            }
            StatusLine(
                text = visibleLabel,
                color = face.ink,
                strong = face.strongLine,
                idle = state == TaffyAssistantPillState.IDLE,
                modifier = labelModifier,
            )
            if (showsTakeOver) {
                PillChip(
                    text = taffyString(R.string.taffy_control_take_over),
                    onClick = onTakeOver,
                    fill = face.chipFill,
                    textColor = face.chipInk,
                    testTag = controlTestTag(TaskControl.TAKE_OVER),
                )
            }
            when (state) {
                TaffyAssistantPillState.IDLE -> Orb()
                else -> face.action?.let { action ->
                    PillChip(
                        text = taffyString(action.labelRes),
                        onClick = onAction,
                        fill = face.chipFill,
                        textColor = face.chipInk,
                        testTag = action.testTag,
                    )
                }
            }
        }
    }
}

/** One state's face: its ground, its ink, the shape it leads with, its control. */
private class PillFace(
    val background: Color,
    val border: Color?,
    val ink: Color,
    val glyph: ImageVector?,
    val glyphSize: Dp,
    val strongLine: Boolean,
    val showsComet: Boolean,
    val paddingEnd: Dp,
    /** Whether this state is one a person can still steer. */
    val takesControls: Boolean,
    /** One chip skin per state, so take over and the state's own chip match. */
    val chipFill: Color,
    val chipInk: Color,
    val action: PillActionFace?,
)

/** The one control a state carries beside take over, when it carries one. */
private class PillActionFace(
    @param:StringRes val labelRes: Int,
    val testTag: String,
)

/**
 * The nine faces, in one place.
 *
 * One `when` rather than one per property, so a new state cannot be given a
 * background and forgotten a control: the compiler asks for the whole face at
 * once, and the four final rows read the design document's task-state table
 * through [taffyStatusSkin] rather than restating it.
 */
@Composable
private fun pillFace(state: TaffyAssistantPillState): PillFace {
    val colors = TaffyTheme.colors
    val isDark = TaffyTheme.isDark
    return when (state) {
        TaffyAssistantPillState.IDLE -> PillFace(
            // The address pill's own family — raised, with the outline. The
            // idle pill used to take the sunken step, which reads as recessed
            // only when the ground under it is the theme surface; in the
            // browser's chrome the ground is the page's own colour, and a
            // sunken tone floating on an arbitrary page reads as a stain
            // rather than a control.
            background = colors.surfaceRaised,
            border = colors.outline,
            ink = colors.textPrimary,
            glyph = null,
            glyphSize = StatusIconSize,
            strongLine = false,
            showsComet = false,
            paddingEnd = PillPaddingEndIdle,
            takesControls = false,
            chipFill = colors.textPrimary,
            chipInk = colors.surface,
            action = null,
        )
        TaffyAssistantPillState.RUNNING -> PillFace(
            background = if (isDark) colors.accentWash else colors.textPrimary,
            border = null,
            ink = if (isDark) colors.accentText else colors.surface,
            glyph = null,
            glyphSize = StatusIconSize,
            strongLine = false,
            showsComet = true,
            paddingEnd = PillPaddingEnd,
            takesControls = true,
            // The handoff's chip is a constant warm dark on the ink pill; the
            // token-honest inverse — paper chip, ink glyph — reads the same in
            // both themes and is one skin for pause and for take over.
            chipFill = colors.surface,
            chipInk = colors.textPrimary,
            action = null,
        )
        TaffyAssistantPillState.WAITING -> PillFace(
            background = colors.accent,
            border = null,
            ink = colors.accentOn,
            glyph = TaffyIcon.Hand,
            glyphSize = HandIconSize,
            strongLine = true,
            showsComet = false,
            paddingEnd = PillPaddingEnd,
            takesControls = true,
            chipFill = colors.accentOn,
            chipInk = colors.accentText,
            action = PillActionFace(R.string.taffy_assistant_review, PILL_REVIEW_TEST_TAG),
        )
        TaffyAssistantPillState.PAUSED -> PillFace(
            background = Color.Transparent,
            border = colors.outline,
            ink = colors.textSecondary,
            // The way out, not the way in. This drew a pause glyph on a pill
            // that was already paused, next to a chip that resumes — the one
            // surface in the product whose glyph is read as what happens next,
            // saying the opposite of what would.
            glyph = TaffyIcon.Play,
            glyphSize = PausedIconSize,
            strongLine = false,
            showsComet = false,
            paddingEnd = PillPaddingEnd,
            takesControls = true,
            chipFill = colors.textPrimary,
            chipInk = colors.surface,
            // The chip performs the resume rather than navigating to it, so it
            // is named as the control it is, by the same tag every other
            // surface's resume carries.
            action = PillActionFace(
                R.string.taffy_assistant_resume,
                controlTestTag(TaskControl.RESUME),
            ),
        )
        // The paused pill without the chip. No rail, because the line has just
        // said nothing is moving, and no Resume, because nothing here promises
        // the reducer would accept one.
        TaffyAssistantPillState.HELD -> PillFace(
            background = Color.Transparent,
            border = colors.outline,
            ink = colors.textSecondary,
            // Not [TaffyIcon.Play] here, which would offer a resume this pill
            // has no chip for, and not [TaffyIcon.Pause], which reads as a
            // control. A held task is waiting on something nobody in this
            // composition can name, and a clock is the honest glyph for that.
            glyph = TaffyIcon.Clock,
            glyphSize = PausedIconSize,
            strongLine = false,
            showsComet = false,
            paddingEnd = PillPaddingStart,
            // A task nothing is driving is still a task on this page, and
            // taking the page over is exactly what a person does about it.
            takesControls = true,
            chipFill = colors.textPrimary,
            chipInk = colors.surface,
            action = null,
        )
        TaffyAssistantPillState.DONE ->
            finalFace(TaffyStatusStyle.DONE, TaffyIcon.CheckCircle)
        TaffyAssistantPillState.PARTLY_DONE ->
            finalFace(TaffyStatusStyle.PARTLY_DONE, TaffyIcon.CircleHalf)
        TaffyAssistantPillState.STOPPED ->
            finalFace(TaffyStatusStyle.STOPPED, TaffyIcon.StopCircle)
        TaffyAssistantPillState.FAILED ->
            finalFace(TaffyStatusStyle.FAILED, TaffyIcon.Warning)
    }
}

/**
 * A final state's face, read from the design document's task-state table.
 *
 * The four of them differ only in which row of that table they take and which
 * shape repeats it, so they share this rather than restating a wash each.
 */
@Composable
private fun finalFace(style: TaffyStatusStyle, glyph: ImageVector): PillFace {
    val skin = taffyStatusSkin(style)
    return PillFace(
        background = skin.background,
        border = skin.border,
        ink = skin.content,
        glyph = glyph,
        glyphSize = StatusIconSize,
        strongLine = false,
        showsComet = false,
        // Nothing trails the line, so the pill closes on the same step it
        // opened with rather than on a chip's inset.
        paddingEnd = PillPaddingStart,
        // Nothing left to steer. The row around the pill gets its own controls
        // back at this exact moment for the same reason (decision 0141).
        takesControls = false,
        chipFill = skin.content,
        chipInk = skin.background,
        action = null,
    )
}

/** The one status line a state shows. */
@Composable
private fun RowScope.StatusLine(
    text: String,
    color: Color,
    strong: Boolean,
    idle: Boolean,
    modifier: Modifier = Modifier,
) {
    TaffyMarqueeText(
        text = text,
        style = when {
            idle -> TaffyTheme.typography.body
            strong -> TaffyTheme.typography.label
            else -> TaffyTheme.typography.caption
        },
        color = color,
        reducedMotion = TaffyTheme.reducedMotion,
        modifier = Modifier.weight(1f),
        textModifier = modifier,
    )
}

/** The idle invitation's ink orb. */
@Composable
private fun Orb() {
    Box(
        modifier = Modifier
            .size(OrbSize)
            .clip(TaffyTheme.shapes.pill)
            .background(TaffyTheme.colors.textPrimary),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            imageVector = TaffyIcon.Waveform,
            contentDescription = null,
            modifier = Modifier.size(OrbIconSize),
            tint = TaffyTheme.colors.surface,
        )
    }
}

/**
 * The one control a waiting or paused pill carries: Review or Resume.
 *
 * A chip with nothing behind it draws nothing. A control the caller did not
 * wire is a control the surface cannot perform, and one that is merely drawn is
 * a promise the reducer has not made.
 */
@Composable
private fun PillChip(
    text: String,
    onClick: (() -> Unit)?,
    fill: Color,
    textColor: Color,
    testTag: String,
) {
    if (onClick == null) return
    Box(
        modifier = Modifier
            .height(ChipTarget)
            .testTag(testTag)
            .clip(TaffyTheme.shapes.pill)
            .clickable(onClick = onClick, role = Role.Button),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            modifier = Modifier
                .height(ChipDrawn)
                .clip(TaffyTheme.shapes.pill)
                .background(fill)
                .padding(horizontal = ChipPadding),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                text = text,
                style = TaffyTheme.typography.label,
                color = textColor,
                maxLines = 1,
            )
        }
    }
}

/**
 * The waiting pill's chip, which is the one control here that is not a
 * [TaskControl] — reviewing an approval opens a surface rather than asking the
 * reducer for anything, so it is named here rather than by [controlTestTag].
 */
const val PILL_REVIEW_TEST_TAG: String = "assistant_pill_review"

// The spec's geometry (handoff/TgAssistantPill.dc.html; px read as dp).
private val PillHeight = 56.dp
private val PillPaddingStart = 14.dp
private val PillPaddingEnd = 4.dp
private val PillPaddingEndIdle = 6.dp
private val OrbSize = 40.dp
private val OrbIconSize = 19.dp
private val ChipTarget = 48.dp

/** What is painted inside that target, so no chip sits against the pill's rim. */
private val ChipDrawn = 44.dp
private val ChipPadding = 14.dp
private val HandIconSize = 15.dp
private val PausedIconSize = 14.dp

/** The final states' leading shape, at the status chip's own proportion. */
private val StatusIconSize = 14.dp

/** One lap of the travelling light, at the start page's own pace. */
private const val CometPeriodMillis = 3200

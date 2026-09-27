// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui.internal

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyButtonVariant

/**
 * The one button body the three public button entry points share
 * (`handoff/TgButton.dc.html`): a fully rounded solid of the variant's skin,
 * at one of the spec's three heights.
 *
 * The skins are symmetric by design — primary is ink on paper and paper on
 * ink, and it is the only fill an action ever gets. A disabled button drops to
 * a whisper of the ink rather than keeping its skin, so it reads as
 * unavailable instead of merely paler.
 */
@Composable
internal fun TaffyButtonBase(
    label: String?,
    onClick: () -> Unit,
    variant: TaffyButtonVariant,
    size: TaffyButtonSize,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    enabled: Boolean = true,
    loading: Boolean = false,
    contentDescription: String? = null,
    testTag: String? = null,
) {
    val colors = TaffyTheme.colors
    val container: Color
    val content: Color
    val stroke: BorderStroke?
    when (variant) {
        TaffyButtonVariant.PRIMARY -> {
            container = colors.textPrimary
            content = colors.surface
            stroke = null
        }
        TaffyButtonVariant.SECONDARY -> {
            container = colors.surfaceRaised
            content = colors.textPrimary
            stroke = BorderStroke(TaffyBorders.standard, colors.outline)
        }
        TaffyButtonVariant.DANGER -> {
            container = colors.dangerWash
            content = colors.dangerText
            // The handoff borders the wash at 30% of the danger hue on paper,
            // 38% on the dark ground. Both are proportions of the hue rather
            // than of the ground, so re-deriving the dark neutrals left them
            // where they were.
            stroke = BorderStroke(
                TaffyBorders.standard,
                colors.dangerText.copy(alpha = if (TaffyTheme.isDark) 0.38f else 0.30f),
            )
        }
    }
    // The label sits between the title and label roles; it derives from the
    // title with the spec's size and weight rather than inventing a role.
    val labelStyle = TaffyTheme.typography.title.copy(
        fontSize = size.labelSize,
        fontWeight = if (variant == TaffyButtonVariant.SECONDARY) {
            FontWeight.W500
        } else {
            FontWeight.W600
        },
    )
    Button(
        onClick = onClick,
        enabled = enabled && !loading,
        modifier = modifier
            .then(
                if (label == null) {
                    Modifier.size(size.height)
                } else {
                    Modifier.heightIn(min = size.height)
                },
            )
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier),
        shape = TaffyTheme.shapes.button,
        colors = ButtonDefaults.buttonColors(
            containerColor = container,
            contentColor = content,
            disabledContainerColor = if (loading) container else colors.textPrimary.copy(alpha = 0.12f),
            disabledContentColor = if (loading) content else colors.textPrimary.copy(alpha = 0.38f),
        ),
        border = stroke,
        contentPadding = if (label == null) {
            PaddingValues()
        } else {
            PaddingValues(horizontal = size.horizontalPadding)
        },
    ) {
        if (loading) {
            CircularProgressIndicator(
                modifier = Modifier.size(size.iconOnlySize),
                color = content,
                strokeWidth = TaffyBorders.rail,
            )
        } else if (icon != null) {
            Icon(
                imageVector = icon,
                contentDescription = contentDescription,
                modifier = Modifier.size(
                    if (label == null) size.iconOnlySize else size.iconWithLabelSize,
                ),
            )
        }
        if (label != null) {
            if (icon != null || loading) {
                Spacer(modifier = Modifier.width(TaffyTheme.spacing.tight))
            }
            Text(
                text = label,
                style = labelStyle,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

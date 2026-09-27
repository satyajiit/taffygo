// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The editable half of the address pill (`handoff/TgAddressPill.dc.html`):
 * screen SCR-103's composer, where the URL is a borderless field inside the
 * same forty-four-unit pill and the trailing action confirms the current
 * reading instead of reloading a page.
 *
 * Stateless like its read-only sibling: the caller owns the text, the
 * placeholder, and what confirm means, and the confirm action hides itself
 * when there is nothing to commit. The boundary uses the control-outline
 * token in both themes so the field remains identifiable at WCAG non-text
 * contrast. [fieldModifier] lands on the field itself, so a screen can tag it
 * for a semantics test.
 *
 * ## The key on the keyboard is the same control as the check mark
 *
 * The field declares an [imeAction], so the keyboard offers Go rather than a
 * key that does nothing, and that key calls [onConfirm] — the same lambda the
 * check mark calls, not a second path that could drift from it. Without this
 * the field was single-line with no action declared, the newline was swallowed,
 * and pressing enter did nothing at all on any TaffyGo screen.
 *
 * When there is nothing to commit, [onConfirm] is null and the keyboard keeps
 * its default behaviour for the action rather than swallowing the press.
 */
@Composable
fun TaffyAddressField(
    value: String,
    onValueChange: (String) -> Unit,
    placeholder: String,
    modifier: Modifier = Modifier,
    onConfirm: (() -> Unit)? = null,
    imeAction: ImeAction = ImeAction.Go,
    fieldModifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier
            .height(FieldHeight)
            .clip(TaffyTheme.shapes.pill)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(
                TaffyBorders.standard,
                TaffyTheme.colors.outline,
                TaffyTheme.shapes.pill,
            )
            .padding(horizontal = FieldPadding),
        horizontalArrangement = Arrangement.spacedBy(FieldGap),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = TaffyIcon.LockSimple,
            contentDescription = taffyString(R.string.taffy_address_pill_secure),
            modifier = Modifier.size(LockSize),
            tint = TaffyTheme.colors.textSecondary,
        )
        BasicTextField(
            value = value,
            onValueChange = onValueChange,
            singleLine = true,
            keyboardOptions = KeyboardOptions(imeAction = imeAction),
            keyboardActions = if (onConfirm == null) {
                KeyboardActions.Default
            } else {
                KeyboardActions { onConfirm() }
            },
            textStyle = TaffyTheme.typography.body.copy(color = TaffyTheme.colors.textPrimary),
            cursorBrush = SolidColor(TaffyTheme.colors.accent),
            modifier = Modifier.weight(1f).then(fieldModifier),
            decorationBox = { field ->
                Box {
                    if (value.isEmpty()) {
                        Text(
                            text = placeholder,
                            style = TaffyTheme.typography.body,
                            color = TaffyTheme.colors.textSecondary,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                    field()
                }
            },
        )
        if (onConfirm != null) {
            Box(
                modifier = Modifier
                    .size(ConfirmTarget)
                    .clickable(onClick = onConfirm, role = Role.Button),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    imageVector = TaffyIcon.Check,
                    contentDescription = taffyString(R.string.taffy_address_field_confirm),
                    modifier = Modifier.size(ConfirmSize),
                    tint = TaffyTheme.colors.textPrimary,
                )
            }
        }
    }
}

// The pill's geometry (handoff/TgAddressPill.dc.html; px read as dp), shared
// with TaffyAddressPill so the composer and the collapsed bar read as one.
private val FieldHeight = 44.dp
private val FieldPadding = 15.dp
private val FieldGap = 9.dp
private val LockSize = 12.dp
private val ConfirmSize = 15.dp

/** Confirm keeps a full touch target even though its glyph renders smaller. */
private val ConfirmTarget = 44.dp

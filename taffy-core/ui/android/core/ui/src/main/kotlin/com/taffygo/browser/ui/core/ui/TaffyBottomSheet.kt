// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The reusable modal sheet frame: one surface, one handle, one heading and a
 * body that clears the navigation bar and keyboard.
 *
 * Features own the fields and actions inside it. Keeping the frame here makes
 * email entry, approvals and future pickers share the same inset and dismissal
 * behaviour instead of each drawing a slightly different sheet.
 *
 * A sheet is the nearest surface on the screen and has to look like it. It
 * used to draw itself in `surface`, the exact colour of the page it covered,
 * which on a dark screen meant a sheet that could not be told from what it had
 * opened over. It now draws in `surfaceSheet` — the top of the tonal ramp —
 * while the scrim separates it from the page. The sheet deliberately has no
 * full-surface border: Material lays that rim across the sheet bounds, and on
 * some sizes its bottom edge appears as an unrelated divider through content.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TaffyBottomSheet(
    title: String,
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    testTag: String? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    // Two toolchains disagree about this call. It is current in the Compose
    // the UI layer builds against and deprecated in the newer Compose the fork
    // carries, whose suggested replacement does not exist in the UI layer's.
    // One source compiles in both, so the suppression stays until the UI layer
    // moves and can then be deleted rather than migrated twice.
    @Suppress("DEPRECATION")
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    ModalBottomSheet(
        onDismissRequest = onDismissRequest,
        sheetState = sheetState,
        modifier = modifier
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier),
        shape = TaffyTheme.shapes.sheet,
        containerColor = TaffyTheme.colors.surfaceSheet,
        contentColor = TaffyTheme.colors.textPrimary,
        dragHandle = { TaffySheetHandle() },
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .windowInsetsPadding(TaffyEdges.bottom)
                .padding(
                    start = TaffyTheme.spacing.screenMargin,
                    top = TaffyTheme.spacing.tight,
                    end = TaffyTheme.spacing.screenMargin,
                    bottom = TaffyTheme.spacing.snug,
                ),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.headline,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.semantics { heading() },
            )
            content()
        }
    }
}

/**
 * The handoff's quiet 34 × 4 dp grab handle.
 *
 * Drawn in `hairline` rather than `outline`. `outline` is the boundary tone,
 * and against the sheet — the lightest surface either theme has — it measures
 * 1.142:1 dark and 1.265:1 light, which is a stroke a person has to look for.
 * `hairline` is the palette's stroke that carries meaning and roughly doubles
 * both, to 2.530:1 and 2.888:1. Both still sit under the 3:1 a graphic object
 * is held to, and that is where this stops: a grab handle is a redundant
 * affordance — the sheet drags, the scrim dismisses it and so does back — and
 * taking it to 3:1 would mean colouring an ornament with a text token.
 */
@Composable
internal fun TaffySheetHandle() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = TaffyTheme.spacing.snug),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            modifier = Modifier
                .size(width = 34.dp, height = 4.dp)
                .clip(TaffyTheme.shapes.pill)
                .background(TaffyTheme.colors.hairline),
        )
    }
}

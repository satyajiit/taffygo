// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.conflate
import kotlinx.coroutines.withContext

/** CommonMark parses off the main thread; streaming updates coalesce without blocking selection. */
@Composable
internal fun TaskAnswerText(segments: List<String>, isStreaming: Boolean) {
    val latest by rememberUpdatedState(segments to isStreaming)
    var rendered by remember { mutableStateOf<RenderedTaskAnswer?>(null) }
    LaunchedEffect(Unit) {
        snapshotFlow { latest }.conflate().collect { (input, streaming) ->
            val blocks = withContext(Dispatchers.Default) {
                TaskAnswerMarkdown.parse(input)
            }
            rendered = RenderedTaskAnswer(input, blocks)
            if (streaming) delay(64)
        }
    }
    val formatted = rendered?.takeIf { it.isPrefixOf(segments) }?.blocks
    SelectionContainer {
        Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
            if (formatted == null) {
                Text(segments.joinToString(""), style = TaffyTheme.typography.body, color = TaffyTheme.colors.textPrimary)
            } else formatted.forEachIndexed { index, block -> key(index) { AnswerBlock(block) } }
        }
    }
}

private data class RenderedTaskAnswer(val segments: List<String>, val blocks: List<TaskAnswerMarkdown.Block>?) {
    /** A new task or replaced answer cannot briefly inherit the preceding answer's formatted text. */
    fun isPrefixOf(current: List<String>): Boolean = segments.size <= current.size &&
        segments.indices.all { index ->
            if (index == segments.lastIndex) current[index].startsWith(segments[index]) else current[index] == segments[index]
        }
}

@Composable
private fun AnswerBlock(block: TaskAnswerMarkdown.Block) {
    val type = TaffyTheme.typography
    val colors = TaffyTheme.colors
    when (block.kind) {
        TaskAnswerMarkdown.Kind.HEADING -> Text(
            block.text,
            style = if (block.level <= 2) type.title else type.body.copy(fontWeight = FontWeight.SemiBold),
            color = colors.textPrimary,
            modifier = Modifier.padding(top = TaffyTheme.spacing.tight).semantics { heading() },
        )
        TaskAnswerMarkdown.Kind.CODE -> Column(
            modifier = Modifier.fillMaxWidth().clip(TaffyTheme.shapes.card)
                .background(colors.surfaceRaised).padding(TaffyTheme.spacing.snug),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            if (block.marker.isNotBlank()) Text(block.marker, style = type.caption, color = colors.textSecondary)
            Text(block.text, style = type.body.copy(fontFamily = FontFamily.Monospace), color = colors.textPrimary,
                modifier = Modifier.horizontalScroll(rememberScrollState()))
        }
        TaskAnswerMarkdown.Kind.QUOTE -> Column(
            modifier = Modifier.fillMaxWidth().clip(TaffyTheme.shapes.card)
                .background(colors.ribbonOneWash).padding(TaffyTheme.spacing.snug),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) { block.children.forEach { AnswerBlock(it) } }
        TaskAnswerMarkdown.Kind.LIST_ITEM -> Row(
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            Text(block.marker, style = type.body, color = colors.textSecondary, modifier = Modifier.widthIn(min = 16.dp))
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
                block.children.forEach { AnswerBlock(it) }
            }
        }
        TaskAnswerMarkdown.Kind.TABLE -> AnswerTable(block.rows)
        TaskAnswerMarkdown.Kind.DIVIDER -> HorizontalDivider(color = colors.outline)
        TaskAnswerMarkdown.Kind.PARAGRAPH -> Text(block.text, style = type.body, color = colors.textPrimary)
    }
}

@Composable
private fun AnswerTable(rows: List<TaskAnswerMarkdown.TableRow>) {
    val columns = rows.maxOfOrNull { it.cells.size } ?: return
    if (columns == 0) return
    BoxWithConstraints(Modifier.fillMaxWidth().clip(TaffyTheme.shapes.card)) {
        val tableWidth = maxOf(maxWidth, 132.dp * columns)
        Column(Modifier.horizontalScroll(rememberScrollState()).width(tableWidth)) {
            rows.forEachIndexed { index, row ->
                Row(Modifier.fillMaxWidth().background(when {
                    row.isHeader -> TaffyTheme.colors.ribbonOneWash
                    index % 2 == 0 -> TaffyTheme.colors.surfaceRaised
                    else -> TaffyTheme.colors.surface
                })) {
                    repeat(columns) { column ->
                        Text(
                            text = row.cells.getOrElse(column) { AnnotatedString("") },
                            style = TaffyTheme.typography.body.let { if (row.isHeader) it.copy(fontWeight = FontWeight.SemiBold) else it },
                            color = TaffyTheme.colors.textPrimary,
                            modifier = Modifier.weight(1f).padding(TaffyTheme.spacing.snug)
                                .then(if (row.isHeader) Modifier.semantics { heading() } else Modifier),
                        )
                    }
                }
            }
        }
    }
}

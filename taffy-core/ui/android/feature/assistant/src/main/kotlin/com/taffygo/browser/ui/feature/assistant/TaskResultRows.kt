// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.ui.factKindLabel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

@Composable
internal fun TaskTimelineStep(entry: TaskTimelineEntry, newest: Boolean) {
    Row(
        modifier = Modifier.fillMaxWidth().testTag("$TIMELINE_ENTRY_TEST_TAG_PREFIX${entry.sequence}").semantics(mergeDescendants = true) {},
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(if (newest) TaffyTheme.colors.textPrimary else TaffyTheme.colors.outline))
        Text(timelineLine(entry), style = TaffyTheme.typography.detail,
            color = if (newest) TaffyTheme.colors.textPrimary else TaffyTheme.colors.textSecondary,
            modifier = Modifier.weight(1f).padding(vertical = TaffyTheme.spacing.step))
    }
}

@Composable
internal fun TaskSourceRow(source: SourceRecord, number: Int, mark: Bitmap? = null) {
    Row(Modifier.fillMaxWidth().testTag("$SOURCE_TEST_TAG_PREFIX${source.id.value}").semantics(mergeDescendants = true) {},
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug), verticalAlignment = Alignment.CenterVertically) {
        SourceMark(number = number, mark = mark)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
            Text(source.title, style = TaffyTheme.typography.label, color = TaffyTheme.colors.textPrimary)
            Text(source.host, style = TaffyTheme.typography.caption, color = TaffyTheme.colors.textSecondary)
            Text(if (source.excluded) taffyString(R.string.taffy_task_view_source_excluded) else
                taffyPlural(R.plurals.taffy_task_view_source_facts, source.factCount, source.factCount),
                style = TaffyTheme.typography.caption, color = TaffyTheme.colors.textSecondary)
        }
    }
}

/**
 * The site's own mark where the profile has one, and its citation number where
 * it does not.
 *
 * The number is not decoration: [TaskFactRow] cites its sources against this
 * list, so the mark is drawn *with* the number rather than instead of it — a
 * favicon that replaced the numbering would leave every fact pointing at
 * nothing. The mark comes from the profile's own favicon store, which is the
 * same store the start page's tiles read and is never a network fetch, so a
 * source list cannot make a request the task did not already make.
 */
@Composable
private fun SourceMark(number: Int, mark: Bitmap?) {
    Box(
        Modifier.size(32.dp).clip(CircleShape)
            .background(if (mark == null) TaffyTheme.colors.ribbonOneWash else TaffyTheme.colors.surfaceRaised),
        contentAlignment = Alignment.Center,
    ) {
        if (mark == null) {
            Text(number.toString(), style = TaffyTheme.typography.caption, color = TaffyTheme.colors.textPrimary)
        } else {
            Image(
                bitmap = mark.asImageBitmap(),
                contentDescription = null,
                contentScale = ContentScale.Fit,
                modifier = Modifier.size(MarkSize),
            )
        }
    }
}

/** Show all supporting sources by their browser-owned identity mapping, never as invented hosts. */
@Composable
internal fun TaskFactRow(
    fact: Fact,
    sources: Map<SourceId, SourceRecord>,
    marks: Map<String, Bitmap> = emptyMap(),
) {
    Column(
        Modifier.fillMaxWidth().clip(TaffyTheme.shapes.card).background(TaffyTheme.colors.surfaceRaised)
            .padding(TaffyTheme.spacing.snug).testTag("$FACT_TEST_TAG_PREFIX${fact.id.value}"),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(fact.field, style = TaffyTheme.typography.caption, color = TaffyTheme.colors.textSecondary)
        Text(fact.value, style = TaffyTheme.typography.title, color = TaffyTheme.colors.textPrimary)
        Text(taffyString(factKindLabel(fact)), style = TaffyTheme.typography.caption, color = TaffyTheme.colors.textSecondary)
        fact.correction?.let { Text(taffyString(R.string.taffy_task_results_correction, it),
            style = TaffyTheme.typography.body, color = TaffyTheme.colors.textPrimary) }
        if (fact.hasConflict) Text(taffyString(R.string.taffy_task_results_conflict),
            style = TaffyTheme.typography.caption, color = TaffyTheme.colors.caution)
        if (fact.needsANewSource) Text(taffyString(R.string.taffy_task_results_new_source),
            style = TaffyTheme.typography.caption, color = TaffyTheme.colors.caution)
        fact.sources.forEach { id ->
            val host = sources[id]?.host?.takeIf { it.isNotBlank() }
            Row(
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                host?.let { marks[it] }?.let { mark ->
                    Image(
                        bitmap = mark.asImageBitmap(),
                        contentDescription = null,
                        contentScale = ContentScale.Fit,
                        modifier = Modifier.size(CitationMarkSize),
                    )
                }
                Text(
                    host ?: taffyString(R.string.taffy_task_results_source_reference, id.value),
                    style = TaffyTheme.typography.caption,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
    }
}

/** Large enough to recognise inside a 32 dp well, small enough to stay in it. */
private val MarkSize = 20.dp

/** A citation's mark sits on a caption line, so it is that line's own height. */
private val CitationMarkSize = 14.dp

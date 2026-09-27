// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.FactKind
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyIconButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.TaffySourceChip
import com.taffygo.browser.ui.core.ui.factKindLabel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** Conflict and lost-source counts as one grouped card, not two bordered rows. */
@Composable
internal fun WorkspaceDetailNotices(state: WorkspaceDetailUiState) {
    if (state.conflictCount == 0 && state.needsANewSourceCount == 0) return
    TaffyGroupedCard {
        if (state.conflictCount > 0) {
            val label = taffyPlural(
                R.plurals.taffy_workspace_detail_conflicts,
                state.conflictCount,
                state.conflictCount,
            )
            WorkspaceRecordRow(
                title = label,
                accessibleDescription = label,
                glyph = TaffyIcon.Warning,
                testTag = CONFLICT_TEST_TAG,
                glyphTaffyInk = true,
                titleTaffyInk = true,
            )
        }
        if (state.conflictCount > 0 && state.needsANewSourceCount > 0) {
            TaffyGroupedCardDivider()
        }
        if (state.needsANewSourceCount > 0) {
            val label = taffyPlural(
                R.plurals.taffy_workspace_detail_needs_sources,
                state.needsANewSourceCount,
                state.needsANewSourceCount,
            )
            WorkspaceRecordRow(
                title = label,
                accessibleDescription = label,
                glyph = TaffyIcon.Warning,
                testTag = NEEDS_SOURCE_TEST_TAG,
            )
        }
    }
}

/** Output facts are individually keyed so the contract-sized table stays virtualized. */
internal fun LazyListScope.workspaceDetailFacts(
    state: WorkspaceDetailUiState,
    sourcesById: Map<SourceId, SourceRecord>,
    onIntent: (WorkspaceDetailIntent) -> Unit,
) {
    item(key = "detail-output-heading", contentType = "heading") {
        TaffySectionHeader(
            title = taffyString(R.string.taffy_workspace_detail_output),
            modifier = Modifier.testTag(DETAIL_OUTPUT_TEST_TAG),
        )
    }
    items(
        items = state.facts,
        key = { it.id.value },
        contentType = { "fact" },
    ) { fact ->
        TaffyGroupedCard {
            val kind = taffyString(factKindLabel(fact))
            val source = fact.sources.firstOrNull()?.let(sourcesById::get)
            WorkspaceRecordRow(
                title = "${fact.field}: ${fact.correction ?: fact.value}",
                supporting = kind,
                accessibleDescription = taffyString(
                    R.string.taffy_workspace_detail_fact_description,
                    fact.field,
                    fact.correction ?: fact.value,
                    kind,
                ),
                glyph = factGlyph(fact),
                testTag = "$DETAIL_FACT_TEST_TAG_PREFIX${fact.id.value}",
                glyphTaffyInk = fact.hasConflict || fact.kind == FactKind.TAFFY_INFERENCE,
                titleTaffyInk = fact.hasConflict,
                onClick = { onIntent(WorkspaceDetailIntent.CorrectFact(fact.id)) },
                trailing = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        source?.let {
                            TaffySourceChip(
                                host = it.host,
                                accessibleDescription = taffyString(
                                    R.string.taffy_workspace_detail_source_chip,
                                    it.host,
                                ),
                                onClick = { onIntent(WorkspaceDetailIntent.OpenSource(it.id)) },
                            )
                        }
                        TaffyIconButton(
                            icon = TaffyIcon.Books,
                            contentDescription = taffyString(
                                R.string.taffy_workspace_detail_keep_fact,
                            ),
                            onClick = { onIntent(WorkspaceDetailIntent.KeepFact(fact.id)) },
                            enabled = state.canKeepFacts && !fact.needsANewSource,
                            testTag = "$DETAIL_KEEP_FACT_TEST_TAG_PREFIX${fact.id.value}",
                        )
                    }
                },
            )
        }
    }
}

const val DETAIL_KEEP_FACT_TEST_TAG_PREFIX: String = "workspace_detail_keep_fact_"

/** Sources are individually keyed. Exclude stays a separate 48 dp control. */
internal fun LazyListScope.workspaceDetailSources(
    state: WorkspaceDetailUiState,
    onIntent: (WorkspaceDetailIntent) -> Unit,
) {
    item(key = "detail-sources-heading", contentType = "heading") {
        TaffySectionHeader(
            title = taffyString(R.string.taffy_workspace_detail_sources),
            modifier = Modifier.testTag(DETAIL_SOURCES_TEST_TAG),
        )
    }
    items(
        items = state.sources,
        key = { it.id.value },
        contentType = { "source" },
    ) { source ->
        TaffyGroupedCard {
            val supporting = if (source.excluded) {
                taffyString(R.string.taffy_workspace_detail_source_excluded)
            } else {
                taffyPlural(
                    R.plurals.taffy_workspace_detail_source_facts,
                    source.factCount,
                    source.factCount,
                )
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                WorkspaceRecordRow(
                    title = source.title,
                    supporting = supporting,
                    accessibleDescription = taffyString(
                        R.string.taffy_workspace_detail_source_description,
                        source.title,
                        source.host,
                    ),
                    glyph = TaffyIcon.GlobeSimple,
                    testTag = "$DETAIL_SOURCE_TEST_TAG_PREFIX${source.id.value}",
                    onClick = { onIntent(WorkspaceDetailIntent.OpenSource(source.id)) },
                    modifier = Modifier.weight(1f),
                )
                if (!source.excluded) {
                    TaffySecondaryButton(
                        label = taffyString(R.string.taffy_workspace_detail_exclude),
                        onClick = { onIntent(WorkspaceDetailIntent.ExcludeSource(source.id)) },
                        testTag = "$DETAIL_EXCLUDE_TEST_TAG_PREFIX${source.id.value}",
                        modifier = Modifier.padding(end = TaffyTheme.spacing.screenMargin),
                    )
                }
            }
        }
    }
}

private fun factGlyph(fact: Fact): ImageVector = when {
    fact.hasConflict || fact.needsANewSource -> TaffyIcon.Warning
    fact.correction != null || fact.kind == FactKind.YOU_ENTERED -> TaffyIcon.PencilSimple
    fact.kind == FactKind.TAFFY_INFERENCE -> TaffyIcon.Sparkle
    else -> TaffyIcon.Article
}

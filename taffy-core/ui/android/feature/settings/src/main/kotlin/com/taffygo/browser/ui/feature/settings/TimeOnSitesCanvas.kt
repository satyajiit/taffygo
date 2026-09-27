// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyHeroCard
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.TaffyPressable
import com.taffygo.browser.ui.core.ui.TaffyStatTile
import com.taffygo.browser.ui.core.ui.TaffyTwoUp
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** Accent-field hero with the 16:9 clay wash aligned to the end. */
@Composable
internal fun TimeOnSitesHero() {
    TaffyHeroCard(
        title = taffyString(R.string.taffy_settings_time_on_sites_title),
        eyebrow = taffyString(R.string.taffy_settings_you_title),
        body = taffyString(R.string.taffy_settings_time_on_sites_summary),
        testTag = TIME_HERO_TEST_TAG,
        illustration = painterResource(R.drawable.taffy_hero_time),
    )
}

@Composable
internal fun TimeOnSitesStats(
    state: TimeOnSitesUiState,
    onIntent: (TimeOnSitesIntent) -> Unit,
) {
    val counted = state.availability == YouSurfaceAvailability.READY
    Column(
        modifier = if (counted) Modifier.testTag(TIME_TOTAL_TEST_TAG) else Modifier,
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaffyTwoUp(
            first = {
                TimeOnSitesStat(
                    range = TimeOnSitesUiState.Range.TODAY,
                    selected = state.range == TimeOnSitesUiState.Range.TODAY,
                    value = if (counted) {
                        formatTimeOnSites(state.todayMillis)
                    } else {
                        taffyString(R.string.taffy_time_not_counted)
                    },
                    label = taffyString(R.string.taffy_time_today),
                    icon = TaffyIcon.Clock,
                    onClick = {
                        onIntent(TimeOnSitesIntent.SelectRange(TimeOnSitesUiState.Range.TODAY))
                    },
                )
            },
            second = {
                TimeOnSitesStat(
                    range = TimeOnSitesUiState.Range.THIS_WEEK,
                    selected = state.range == TimeOnSitesUiState.Range.THIS_WEEK,
                    value = if (counted) {
                        formatTimeOnSites(state.weekMillis)
                    } else {
                        taffyString(R.string.taffy_time_not_counted)
                    },
                    label = taffyString(R.string.taffy_time_this_week),
                    icon = TaffyIcon.ChartBar,
                    onClick = {
                        onIntent(TimeOnSitesIntent.SelectRange(TimeOnSitesUiState.Range.THIS_WEEK))
                    },
                )
            },
            modifier = Modifier.testTag(TIME_RANGE_TEST_TAG),
        )
        if (counted) {
            val count = state.sites.size
            val plural = when (state.range) {
                TimeOnSitesUiState.Range.TODAY -> if (state.sites.any { it.grouped }) {
                    R.plurals.taffy_time_across_today_at_least
                } else {
                    R.plurals.taffy_time_across_today
                }
                TimeOnSitesUiState.Range.THIS_WEEK -> if (state.sites.any { it.grouped }) {
                    R.plurals.taffy_time_across_week_at_least
                } else {
                    R.plurals.taffy_time_across_week
                }
            }
            Text(
                text = taffyPlural(plural, count, count),
                style = TaffyTheme.typography.caption,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

@Composable
private fun TimeOnSitesStat(
    range: TimeOnSitesUiState.Range,
    selected: Boolean,
    value: String,
    label: String,
    icon: ImageVector,
    onClick: () -> Unit,
) {
    val shape = TaffyTheme.shapes.tile
    TaffyPressable(
        onClick = onClick,
        role = Role.Tab,
        testTag = "$TIME_RANGE_TEST_TAG_PREFIX${range.name}",
        modifier = Modifier
            .fillMaxWidth()
            .semantics { this.selected = selected },
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .clip(shape)
                .border(
                    TaffyBorders.emphasis,
                    if (selected) TaffyTheme.colors.textPrimary else Color.Transparent,
                    shape,
                ),
        ) {
            TaffyStatTile(value = value, label = label, icon = icon)
        }
    }
}

@Composable
internal fun TimeOnSitesLoading() {
    TaffyTwoUp(
        first = { TimeOnSitesStatSkeleton() },
        second = { TimeOnSitesStatSkeleton() },
        modifier = Modifier.testTag(TIME_LOADING_TEST_TAG),
    )
}

@Composable
internal fun TimeOnSitesListLoading() {
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        repeat(2) {
            TaffySkeleton(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(SiteCardSkeletonHeight),
                accessibleDescription = if (it == 0) {
                    taffyString(R.string.taffy_time_loading)
                } else {
                    null
                },
            )
        }
    }
}

@Composable
private fun TimeOnSitesStatSkeleton() {
    TaffySkeleton(
        modifier = Modifier
            .fillMaxWidth()
            .height(StatSkeletonHeight),
        shape = TaffyTheme.shapes.tile,
    )
}

@Composable
internal fun TimeOnSitesUnavailable() {
    TaffyInfoTile(testTag = TIME_UNAVAILABLE_TEST_TAG) {
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            Text(
                text = taffyString(R.string.taffy_time_unavailable_title),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.semantics { heading() },
            )
            Text(
                text = taffyString(R.string.taffy_time_unavailable_body),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

@Composable
internal fun TimeOnSitesEmpty() {
    Column(modifier = Modifier.fillMaxWidth().testTag(TIME_EMPTY_TEST_TAG)) {
        TaffyEmptyState(
            title = taffyString(R.string.taffy_time_empty_title),
            body = taffyString(R.string.taffy_time_empty_body),
            leading = {
                Icon(
                    imageVector = TaffyIcon.Clock,
                    contentDescription = null,
                    tint = TaffyTheme.colors.textPrimary,
                    modifier = Modifier.size(SettingsGlyphSize),
                )
            },
        )
    }
}

internal fun LazyListScope.timeOnSitesSites(
    state: TimeOnSitesUiState,
    onIntent: (TimeOnSitesIntent) -> Unit,
) {
    itemsIndexed(
        items = state.sites,
        key = { index, site -> "${site.grouped}:${site.site}:$index" },
        contentType = { _, _ -> "site" },
    ) { _, site ->
        TimeOnSitesSiteCard(
            site = site,
            largestMillis = state.largestMillis,
            onClick = if (site.grouped) {
                null
            } else {
                { onIntent(TimeOnSitesIntent.OpenSite(site.site)) }
            },
        )
    }
}

@Composable
private fun TimeOnSitesSiteCard(
    site: TimeOnSitesUiState.Site,
    largestMillis: Long,
    onClick: (() -> Unit)?,
) {
    val label = if (site.grouped) taffyString(R.string.taffy_time_other_sites) else site.site
    val duration = formatTimeOnSites(site.durationMillis)
    val description = taffyString(R.string.taffy_time_row_description, label, duration)
    val fraction = timeOnSitesBarFraction(site.durationMillis, largestMillis)
    TaffyObjectCard(
        onClick = onClick,
        testTag = "$TIME_SITE_TEST_TAG_PREFIX${if (site.grouped) "other" else site.site}",
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
                .semantics(mergeDescendants = true) { contentDescription = description },
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                SettingsGlyph(TaffyIcon.GlobeSimple)
                Text(
                    text = label,
                    style = TaffyTheme.typography.body,
                    color = TaffyTheme.colors.textPrimary,
                    modifier = Modifier.weight(1f),
                )
                Text(
                    text = duration,
                    style = TaffyTheme.typography.numeric,
                    color = TaffyTheme.colors.textPrimary,
                )
            }
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(BarHeight)
                    .clip(TaffyTheme.shapes.pill)
                    .background(TaffyTheme.colors.surfaceSunken)
                    .clearAndSetSemantics { },
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth(fraction)
                        .height(BarHeight)
                        .clip(TaffyTheme.shapes.pill)
                        .background(TaffyTheme.colors.textPrimary),
                )
            }
        }
    }
}

@Composable
internal fun TimeOnSitesNotes(state: TimeOnSitesUiState) {
    TaffyInfoTile {
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            if (state.clearedWithHistory) {
                Text(
                    text = taffyString(R.string.taffy_time_cleared),
                    style = TaffyTheme.typography.caption,
                    color = TaffyTheme.colors.textSecondary,
                    modifier = Modifier.testTag(TIME_CLEARED_TEST_TAG),
                )
            }
            if (state.hasGroupedSites) {
                Text(
                    text = taffyString(R.string.taffy_time_grouped_note),
                    style = TaffyTheme.typography.caption,
                    color = TaffyTheme.colors.textSecondary,
                    modifier = Modifier.testTag(TIME_GROUPED_TEST_TAG),
                )
            }
            if (state.recoveredFromCorruption) {
                Text(
                    text = taffyString(R.string.taffy_time_recovered_note),
                    style = TaffyTheme.typography.caption,
                    color = TaffyTheme.colors.textSecondary,
                    modifier = Modifier.testTag(TIME_RECOVERED_TEST_TAG),
                )
            }
            Text(
                text = taffyString(R.string.taffy_time_retention_note),
                style = TaffyTheme.typography.caption,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(TIME_RETENTION_TEST_TAG),
            )
            Text(
                text = taffyString(R.string.taffy_time_caption_private),
                style = TaffyTheme.typography.caption,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(TIME_CAPTION_PRIVATE_TEST_TAG),
            )
            Text(
                text = taffyString(R.string.taffy_time_caption_taffy),
                style = TaffyTheme.typography.caption,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(TIME_CAPTION_TAFFY_TEST_TAG),
            )
        }
    }
}

private val BarHeight = 8.dp
private val StatSkeletonHeight = 96.dp
private val SiteCardSkeletonHeight = 88.dp

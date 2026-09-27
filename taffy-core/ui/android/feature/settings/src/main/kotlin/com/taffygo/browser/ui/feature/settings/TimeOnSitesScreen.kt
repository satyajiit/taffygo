// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffySectionBar
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-411 — Time on sites. */
@Composable
fun TimeOnSitesScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: TimeOnSitesViewModel = screenViewModel(TaffyDestination.TimeOnSites)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    TimeOnSitesContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The canvas: hero, section bars, stacked stats, object cards, distinct empty. */
@Composable
fun TimeOnSitesContent(
    state: TimeOnSitesUiState,
    onIntent: (TimeOnSitesIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    val hasSites = state.availability == YouSurfaceAvailability.READY && state.sites.isNotEmpty()
    TaffyLazyScreen(
        destination = TaffyDestination.TimeOnSites,
        title = taffyString(R.string.taffy_settings_time_on_sites_title),
        onBack = onBack,
        modifier = modifier,
        listModifier = Modifier.testTag(
            if (hasSites) TIME_LIST_TEST_TAG else TIME_SCROLL_TEST_TAG,
        ),
    ) {
        item(key = "time-hero", contentType = "hero") { TimeOnSitesHero() }
        item(key = "time-counts-heading", contentType = "heading") {
            TaffySectionBar(
                title = taffyString(R.string.taffy_time_counts_title),
                eyebrow = taffyString(R.string.taffy_time_counts_eyebrow),
            )
        }
        when (state.availability) {
            YouSurfaceAvailability.LOADING -> item(
                key = "time-stats-loading",
                contentType = "status",
            ) {
                TimeOnSitesLoading()
            }
            YouSurfaceAvailability.UNAVAILABLE,
            YouSurfaceAvailability.READY,
            -> item(key = "time-stats", contentType = "stats") {
                TimeOnSitesStats(state, onIntent)
            }
        }
        item(key = "time-sites-heading", contentType = "heading") {
            TaffySectionBar(
                title = taffyString(
                    if (state.range == TimeOnSitesUiState.Range.TODAY) {
                        R.string.taffy_time_today
                    } else {
                        R.string.taffy_time_this_week
                    },
                ),
                eyebrow = taffyString(R.string.taffy_time_sites_title),
            )
        }
        when (state.availability) {
            YouSurfaceAvailability.LOADING -> item(
                key = "time-list-loading",
                contentType = "status",
            ) {
                TimeOnSitesListLoading()
            }
            YouSurfaceAvailability.UNAVAILABLE -> item(
                key = "time-unavailable",
                contentType = "status",
            ) {
                TimeOnSitesUnavailable()
            }
            YouSurfaceAvailability.READY -> if (state.sites.isEmpty()) {
                item(key = "time-empty", contentType = "status") { TimeOnSitesEmpty() }
            } else {
                timeOnSitesSites(state, onIntent)
            }
        }
        item(key = "time-notes", contentType = "notes") { TimeOnSitesNotes(state) }
    }
}

@Composable
internal fun formatTimeOnSites(durationMillis: Long): String {
    if (durationMillis <= 0L) return taffyString(R.string.taffy_time_zero)
    val (hours, minutes) = timeOnSitesParts(durationMillis)
    return when {
        hours > 0 -> taffyString(R.string.taffy_time_hours_minutes, hours, minutes)
        minutes > 0 -> taffyString(R.string.taffy_time_minutes, minutes)
        else -> taffyString(R.string.taffy_time_less_than_minute)
    }
}

/** Tags screen SCR-411's semantics tests name. */
const val TIME_RANGE_TEST_TAG: String = "time_on_sites_range"
const val TIME_RANGE_TEST_TAG_PREFIX: String = "time_on_sites_range_"
const val TIME_TOTAL_TEST_TAG: String = "time_on_sites_total"
const val TIME_LIST_TEST_TAG: String = "time_on_sites_list"
const val TIME_SCROLL_TEST_TAG: String = "time_on_sites_scroll"
const val TIME_SITE_TEST_TAG_PREFIX: String = "time_on_sites_site_"
const val TIME_CAPTION_PRIVATE_TEST_TAG: String = "time_on_sites_caption_private"
const val TIME_CAPTION_TAFFY_TEST_TAG: String = "time_on_sites_caption_taffy"
const val TIME_CLEARED_TEST_TAG: String = "time_on_sites_cleared"
const val TIME_GROUPED_TEST_TAG: String = "time_on_sites_grouped"
const val TIME_RETENTION_TEST_TAG: String = "time_on_sites_retention"
const val TIME_RECOVERED_TEST_TAG: String = "time_on_sites_recovered"
const val TIME_HERO_TEST_TAG: String = "time_on_sites_hero"
const val TIME_UNAVAILABLE_TEST_TAG: String = "time_on_sites_unavailable"
const val TIME_EMPTY_TEST_TAG: String = "time_on_sites_empty"
const val TIME_LOADING_TEST_TAG: String = "time_on_sites_loading"

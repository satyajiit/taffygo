// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.annotation.DrawableRes
import androidx.annotation.RawRes

/**
 * One rendered feature explainer with compile-checked resources for both themes.
 *
 * ## Why there are four of these and not seven
 *
 * Seven explainers were rendered. Three of them — `compare`, `driving-licence`
 * and `mail` — composite official Amazon, Flipkart, Croma, Reliance Digital,
 * Samsung, DigiLocker, Ministry of Road Transport and Highways and Gmail
 * artwork. Their provenance record states in its own words that an official
 * source is provenance and not permission, and OD-094 in
 * `docs/open-decisions.md` is the launch blocker that owns the mark-by-mark
 * decision for all eight owners. Those three are not in the tree.
 *
 * The four here carry no third-party logo or product artwork at all: the only
 * non-TaffyGo material in them is Space Grotesk, rasterised into the frames
 * under the SIL Open Font License 1.1, and `page-tools` additionally shows this
 * repository's own `test-fixtures/web/origins/primary/media/diagram.svg`. The
 * per-stem statement is in `vendor/showcase-films.txt`, which also carries the
 * eight owners' official sources for the three that are held.
 *
 * The held stems are recoverable from Git the moment a permission is recorded
 * per owner. Re-rendering them without the marks is not available: the authoring
 * project the record's own remedy names was ignored by Git and is not on any
 * host.
 *
 * The four keep their positions from the authored sequence of seven rather
 * than being renumbered, so the order a person sees is still the order the
 * films were written to be watched in.
 */
internal enum class ShowcaseVideo(
    @RawRes private val lightVideo: Int,
    @RawRes private val darkVideo: Int,
    @DrawableRes private val lightFinalPoster: Int,
    @DrawableRes private val darkFinalPoster: Int,
) {
    FORM(
        R.raw.taffy_showcase_form_light,
        R.raw.taffy_showcase_form_dark,
        R.drawable.taffy_showcase_form_light_poster,
        R.drawable.taffy_showcase_form_dark_poster,
    ),
    WEEKLY_REVIEW(
        R.raw.taffy_showcase_weekly_review_light,
        R.raw.taffy_showcase_weekly_review_dark,
        R.drawable.taffy_showcase_weekly_review_light_poster,
        R.drawable.taffy_showcase_weekly_review_dark_poster,
    ),
    JOB(
        R.raw.taffy_showcase_job_light,
        R.raw.taffy_showcase_job_dark,
        R.drawable.taffy_showcase_job_light_poster,
        R.drawable.taffy_showcase_job_dark_poster,
    ),
    PAGE_TOOLS(
        R.raw.taffy_showcase_page_tools_light,
        R.raw.taffy_showcase_page_tools_dark,
        R.drawable.taffy_showcase_page_tools_light_poster,
        R.drawable.taffy_showcase_page_tools_dark_poster,
    ),
    ;

    @RawRes
    fun videoResourceId(isDark: Boolean): Int = if (isDark) darkVideo else lightVideo

    @DrawableRes
    fun posterResourceId(isDark: Boolean, finalFrame: Boolean): Int {
        if (!finalFrame) {
            return if (isDark) {
                R.drawable.taffy_showcase_start_dark_poster
            } else {
                R.drawable.taffy_showcase_start_light_poster
            }
        }
        return if (isDark) darkFinalPoster else lightFinalPoster
    }
}

/** The rendered explainers are at most ten seconds long. */
internal const val MAX_SHOWCASE_VIDEO_DURATION_MS: Long = 10_000L

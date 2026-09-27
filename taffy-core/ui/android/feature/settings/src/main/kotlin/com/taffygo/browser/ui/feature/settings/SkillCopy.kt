// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.graphics.vector.ImageVector
import com.taffygo.browser.ui.core.ui.TaffyIcon

internal fun skillTitleRes(id: String): Int? = when (id) {
    SkillsRepository.GENERAL_WEB_RESEARCH -> R.string.taffy_skill_pages_lookup
    SkillsRepository.DEEP_RESEARCH -> R.string.taffy_skill_depth
    SkillsRepository.PRODUCT_COMPARISON -> R.string.taffy_skill_products
    SkillsRepository.MULTI_TAB_COMPARISON -> R.string.taffy_skill_pages_compare
    SkillsRepository.WEBSITE_SUMMARIZER -> R.string.taffy_skill_pages_summarize
    SkillsRepository.PDF_ANALYSIS -> R.string.taffy_skill_pdf
    SkillsRepository.DATA_EXTRACTION -> R.string.taffy_skill_pages_table
    SkillsRepository.FORM_ASSISTANT -> R.string.taffy_skill_form
    SkillsRepository.SHOPPING -> R.string.taffy_skill_offers
    SkillsRepository.DOWNLOAD_ORGANIZER -> R.string.taffy_skill_downloads
    SkillsRepository.TRAVEL_RESEARCH -> R.string.taffy_skill_trip
    SkillsRepository.VIDEO_TRANSCRIPT_ANALYZER -> R.string.taffy_skill_video
    SkillsRepository.IMAGE_UNDERSTANDING -> R.string.taffy_skill_pictures
    SkillsRepository.LIBRARY_BUILDER -> R.string.taffy_skill_keep
    SkillsRepository.SPREADSHEET_BUILDER -> R.string.taffy_skill_sheet
    SkillsRepository.DOCUMENT_GENERATOR -> R.string.taffy_skill_document
    else -> null
}

internal fun skillSummaryRes(id: String): Int? = when (id) {
    SkillsRepository.GENERAL_WEB_RESEARCH -> R.string.taffy_skill_pages_lookup_summary
    SkillsRepository.DEEP_RESEARCH -> R.string.taffy_skill_depth_summary
    SkillsRepository.PRODUCT_COMPARISON -> R.string.taffy_skill_products_summary
    SkillsRepository.MULTI_TAB_COMPARISON -> R.string.taffy_skill_pages_compare_summary
    SkillsRepository.WEBSITE_SUMMARIZER -> R.string.taffy_skill_pages_summarize_summary
    SkillsRepository.PDF_ANALYSIS -> R.string.taffy_skill_pdf_summary
    SkillsRepository.DATA_EXTRACTION -> R.string.taffy_skill_pages_table_summary
    SkillsRepository.FORM_ASSISTANT -> R.string.taffy_skill_form_summary
    SkillsRepository.SHOPPING -> R.string.taffy_skill_offers_summary
    SkillsRepository.DOWNLOAD_ORGANIZER -> R.string.taffy_skill_downloads_summary
    SkillsRepository.TRAVEL_RESEARCH -> R.string.taffy_skill_trip_summary
    SkillsRepository.VIDEO_TRANSCRIPT_ANALYZER -> R.string.taffy_skill_video_summary
    SkillsRepository.IMAGE_UNDERSTANDING -> R.string.taffy_skill_pictures_summary
    SkillsRepository.LIBRARY_BUILDER -> R.string.taffy_skill_keep_summary
    SkillsRepository.SPREADSHEET_BUILDER -> R.string.taffy_skill_sheet_summary
    SkillsRepository.DOCUMENT_GENERATOR -> R.string.taffy_skill_document_summary
    else -> null
}

internal fun skillGroupTitleRes(group: SkillsRepository.Group): Int = when (group) {
    SkillsRepository.Group.PAGES -> R.string.taffy_skills_group_pages
    SkillsRepository.Group.SHOPPING -> R.string.taffy_skills_group_shopping
    SkillsRepository.Group.FORMS -> R.string.taffy_skills_group_forms
    SkillsRepository.Group.RESEARCH -> R.string.taffy_skills_group_research
    SkillsRepository.Group.MEDIA -> R.string.taffy_skills_group_media
    SkillsRepository.Group.LIBRARY -> R.string.taffy_skills_group_library
}

/** One regular Phosphor mark per installed ability. Never a fill weight. */
internal fun skillIcon(id: String): ImageVector = when (id) {
    SkillsRepository.GENERAL_WEB_RESEARCH -> TaffyIcon.MagnifyingGlass
    SkillsRepository.DEEP_RESEARCH -> TaffyIcon.GlobeSimple
    SkillsRepository.PRODUCT_COMPARISON -> TaffyIcon.Tag
    SkillsRepository.MULTI_TAB_COMPARISON -> TaffyIcon.SquaresFour
    SkillsRepository.WEBSITE_SUMMARIZER -> TaffyIcon.Article
    SkillsRepository.PDF_ANALYSIS -> TaffyIcon.CopySimple
    SkillsRepository.DATA_EXTRACTION -> TaffyIcon.Table
    SkillsRepository.FORM_ASSISTANT -> TaffyIcon.IdentificationCard
    SkillsRepository.SHOPPING -> TaffyIcon.Star
    SkillsRepository.DOWNLOAD_ORGANIZER -> TaffyIcon.DownloadSimple
    SkillsRepository.TRAVEL_RESEARCH -> TaffyIcon.Flag
    SkillsRepository.VIDEO_TRANSCRIPT_ANALYZER -> TaffyIcon.Monitor
    SkillsRepository.IMAGE_UNDERSTANDING -> TaffyIcon.Image
    SkillsRepository.LIBRARY_BUILDER -> TaffyIcon.BookmarkSimple
    SkillsRepository.SPREADSHEET_BUILDER -> TaffyIcon.ChartBar
    SkillsRepository.DOCUMENT_GENERATOR -> TaffyIcon.PencilSimple
    else -> TaffyIcon.PuzzlePiece
}

internal fun skillMayUseRes(mayUse: SkillsRepository.MayUse): Int = when (mayUse) {
    SkillsRepository.MayUse.PAGES -> R.string.taffy_skill_may_use_pages
    SkillsRepository.MayUse.FORM -> R.string.taffy_skill_may_use_form
    SkillsRepository.MayUse.DOWNLOADS -> R.string.taffy_skill_may_use_downloads
    SkillsRepository.MayUse.FILES -> R.string.taffy_skill_may_use_files
    SkillsRepository.MayUse.LIBRARY -> R.string.taffy_skill_may_use_library
    SkillsRepository.MayUse.PICTURES -> R.string.taffy_skill_may_use_pictures
    SkillsRepository.MayUse.VIDEO -> R.string.taffy_skill_may_use_video
}

internal fun skillReadinessRes(readiness: SkillsRepository.Readiness): Int = when (readiness) {
    SkillsRepository.Readiness.READY -> R.string.taffy_skills_on
    SkillsRepository.Readiness.REQUIRED_TOOL_UNAVAILABLE ->
        R.string.taffy_skill_required_tool_unavailable
    SkillsRepository.Readiness.REQUIRED_PART_MISSING -> R.string.taffy_skill_required_part_missing
    SkillsRepository.Readiness.PROFILE_UNAVAILABLE -> R.string.taffy_skill_profile_unavailable
    SkillsRepository.Readiness.POLICY_UNAVAILABLE -> R.string.taffy_skill_policy_unavailable
}

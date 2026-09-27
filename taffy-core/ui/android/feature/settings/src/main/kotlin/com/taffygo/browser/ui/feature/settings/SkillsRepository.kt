// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.SavedFlowReview
import kotlinx.coroutines.flow.StateFlow
import taffy.core_api.SiteSkillObservedClause
import taffy.core_api.SiteSkillObservedStep

/**
 * Installed abilities of the one assistant.
 *
 * A toggle never grants a new permission. Built-in rows look installed. There
 * is no remote catalog on this port.
 */
interface SkillsRepository {

    /** What screens SCR-601 and SCR-602 render. */
    val snapshot: StateFlow<Snapshot>

    /** Turn one installed ability on or off. */
    suspend fun setEnabled(id: String, enabled: Boolean)

    /** Fetch one exact current review without enabling it. */
    suspend fun loadRecordedReview(id: String, version: UInt): Boolean = false

    /** Accept only the complete immutable recording that the person reviewed. */
    suspend fun acceptRecorded(review: SavedFlowReview): MutationResult = MutationResult.UNAVAILABLE

    /** Remove an added ability. Built-in rows refuse. */
    suspend fun remove(id: String): RemoveResult

    /**
     * Teach one site from a recording the browser already observed.
     *
     * A recording contains only closed semantic clauses, compiled tool names,
     * and reference-shaped arguments. There is deliberately no source-code,
     * selector, or free-form page-payload field on this seam.
     */
    suspend fun teachSite(name: String, recording: ObservedSiteRecording): MutationResult

    /** Replace one recorded definition at the exact version currently shown. */
    suspend fun updateSite(
        id: String,
        expectedVersion: UInt,
        recording: ObservedSiteRecording,
    ): MutationResult

    /** One reading of the installed list. */
    data class Snapshot(
        val availability: Availability,
        val skills: List<Skill> = emptyList(),
        /** False when recovery omitted added site abilities but retained built-ins. */
        val siteSkillsAvailable: Boolean = true,
    )

    /** One installed ability. */
    data class Skill(
        val id: String,
        val group: Group,
        val mayUse: List<MayUse>,
        val enabled: Boolean,
        val builtIn: Boolean,
        val readiness: Readiness = Readiness.READY,
        val origin: String? = null,
        val version: UInt = 0u,
        val stepCount: UInt = 0u,
        val lifecycle: Lifecycle = Lifecycle.ACTIVE,
        val needsRecordedReview: Boolean = false,
        val recorded: Boolean = false,
        val review: SavedFlowReview? = null,
    )

    /** A browser-derived definition with no executable-code field. */
    data class ObservedSiteRecording(
        val origin: String,
        val clauses: List<SiteSkillObservedClause>,
        val steps: List<SiteSkillObservedStep>,
    )

    /** Whether the list is here, still arriving, or missing. */
    enum class Availability { LOADING, READY, UNAVAILABLE }

    /** Why a visible ability can or cannot run in this profile right now. */
    enum class Readiness {
        READY,
        REQUIRED_TOOL_UNAVAILABLE,
        REQUIRED_PART_MISSING,
        PROFILE_UNAVAILABLE,
        POLICY_UNAVAILABLE,
    }

    /** The job a built-in ability belongs to. */
    enum class Group { PAGES, SHOPPING, FORMS, RESEARCH, MEDIA, LIBRARY }

    /** What one ability may touch, said as a user-facing line. */
    enum class MayUse { PAGES, FORM, DOWNLOADS, FILES, LIBRARY, PICTURES, VIDEO }

    /** The durable lifecycle Rust published for an added site skill. */
    enum class Lifecycle { DRAFT, ACTIVE, SUPERSEDED, RETIRED, DISABLED }

    /** How a remove ended. */
    enum class RemoveResult { REMOVED, CANNOT_REMOVE, NOT_FOUND, UNAVAILABLE }

    /** Admission of a teach or exact-version update; publication remains authoritative. */
    enum class MutationResult { SUBMITTED, INVALID, ALREADY_EXISTS, NOT_FOUND, STALE, UNAVAILABLE }

    companion object {
        const val GENERAL_WEB_RESEARCH: String = "general-web-research"
        const val DEEP_RESEARCH: String = "deep-research"
        const val PRODUCT_COMPARISON: String = "product-comparison"
        const val MULTI_TAB_COMPARISON: String = "multi-tab-comparison"
        const val WEBSITE_SUMMARIZER: String = "website-summarizer"
        const val PDF_ANALYSIS: String = "pdf-analysis"
        const val DATA_EXTRACTION: String = "data-extraction"
        const val FORM_ASSISTANT: String = "form-assistant"
        const val SHOPPING: String = "shopping"
        const val DOWNLOAD_ORGANIZER: String = "download-organizer"
        const val TRAVEL_RESEARCH: String = "travel-research"
        const val VIDEO_TRANSCRIPT_ANALYZER: String = "video-transcript-analyzer"
        const val IMAGE_UNDERSTANDING: String = "image-understanding"
        const val LIBRARY_BUILDER: String = "library-builder"
        const val SPREADSHEET_BUILDER: String = "spreadsheet-builder"
        const val DOCUMENT_GENERATOR: String = "document-generator"

        /** Deterministic visual fixture for previews and reducer tests only. */
        fun previewBuiltIns(): List<Skill> = listOf(
            Skill(GENERAL_WEB_RESEARCH, Group.PAGES, listOf(MayUse.PAGES), true, true),
            Skill(DEEP_RESEARCH, Group.RESEARCH, listOf(MayUse.PAGES), true, true),
            Skill(PRODUCT_COMPARISON, Group.SHOPPING, listOf(MayUse.PAGES), true, true),
            Skill(MULTI_TAB_COMPARISON, Group.PAGES, listOf(MayUse.PAGES), true, true),
            Skill(WEBSITE_SUMMARIZER, Group.PAGES, listOf(MayUse.PAGES), true, true),
            Skill(PDF_ANALYSIS, Group.MEDIA, listOf(MayUse.FILES), true, true),
            Skill(DATA_EXTRACTION, Group.PAGES, listOf(MayUse.PAGES), true, true),
            Skill(FORM_ASSISTANT, Group.FORMS, listOf(MayUse.FORM), true, true),
            Skill(SHOPPING, Group.SHOPPING, listOf(MayUse.PAGES), true, true),
            Skill(
                DOWNLOAD_ORGANIZER,
                Group.FORMS,
                listOf(MayUse.DOWNLOADS),
                enabled = true,
                builtIn = true,
                readiness = Readiness.REQUIRED_TOOL_UNAVAILABLE,
            ),
            Skill(TRAVEL_RESEARCH, Group.RESEARCH, listOf(MayUse.PAGES), true, true),
            Skill(VIDEO_TRANSCRIPT_ANALYZER, Group.MEDIA, listOf(MayUse.VIDEO), true, true),
            Skill(IMAGE_UNDERSTANDING, Group.MEDIA, listOf(MayUse.PICTURES), true, true),
            Skill(LIBRARY_BUILDER, Group.LIBRARY, listOf(MayUse.LIBRARY), true, true),
            Skill(SPREADSHEET_BUILDER, Group.FORMS, listOf(MayUse.FILES), true, true),
            Skill(DOCUMENT_GENERATOR, Group.FORMS, listOf(MayUse.FILES), true, true),
        )
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import com.taffygo.browser.ui.core.model.SavedFlowReview
import java.net.URI
import taffy.core_api.SiteSkillArgumentKind
import taffy.core_api.SiteSkillMutationBody
import taffy.core_api.SiteSkillMutationKind
import taffy.core_api.SiteSkillObservedStep
import taffy.core_api.SiteSkillProvenanceView
import taffy.core_api.SiteSkillStatusView
import taffy.core_api.SiteSkillView

/** A recorded draft must be reviewed before its first activation, including legacy drafts. */
fun SiteSkillView.needsRecordedReview(): Boolean =
    provenance == SiteSkillProvenanceView.RECORDED_FROM_TASK && status == SiteSkillStatusView.DRAFT

/**
 * Projects only a complete review the UI knows how to explain. Legacy metadata and unknown
 * action descriptions cannot silently become an acceptance of an unseen definition.
 * The core remains responsible for validating and executing the definition.
 */
fun SiteSkillView.toSavedFlowReview(): SavedFlowReview? {
    if (provenance != SiteSkillProvenanceView.RECORDED_FROM_TASK ||
        reviewed_steps.isEmpty() || reviewed_steps.size.toUInt() != step_count
    ) return null
    val steps = reviewed_steps.map { it.reviewStep() ?: return null }
    val first = steps.first()
    if (first.action != SavedFlowReview.Action.OPEN_PAGE) return null
    return SavedFlowReview(
        id = skill_id,
        version = active_version,
        origin = origin,
        startingAddress = first.address ?: return null,
        steps = steps,
    )
}

/** Exact version from the review, never a replacement read after the person tapped Save. */
fun SavedFlowReview.acceptanceMutation(): SiteSkillMutationBody = SiteSkillMutationBody(
    kind = SiteSkillMutationKind.SET_ENABLED,
    skill_id = id,
    expected_version = version,
    origin = "",
    clauses = emptyList(),
    steps = emptyList(),
    admitted = 0u,
    enabled = true,
    recorded_at_epoch_ms = 0uL,
)

private fun SiteSkillObservedStep.reviewStep(): SavedFlowReview.Step? {
    val action = when (verb) {
        "browser.navigate" -> SavedFlowReview.Action.OPEN_PAGE
        "browser.dom.read", "browser.selection.read" -> SavedFlowReview.Action.READ_PAGE
        "browser.dom.query" -> SavedFlowReview.Action.FIND_CONTROL
        "browser.dom.click" -> SavedFlowReview.Action.CHOOSE_CONTROL
        "browser.dom.focus" -> SavedFlowReview.Action.FOCUS_CONTROL
        "browser.link.open" -> SavedFlowReview.Action.OPEN_LINK
        "browser.dom.scroll" -> SavedFlowReview.Action.SCROLL
        "user.handover" -> SavedFlowReview.Action.HANDOVER
        "browser.form.inspect" -> SavedFlowReview.Action.INSPECT_FORM
        "browser.form.fill" -> SavedFlowReview.Action.FILL_FORM
        "browser.form.submit" -> SavedFlowReview.Action.SUBMIT_FORM
        "browser.download.start", "browser.download.from_link" -> SavedFlowReview.Action.DOWNLOAD
        "browser.download.list" -> SavedFlowReview.Action.READ_DOWNLOADS
        "page.pdf.inspect" -> SavedFlowReview.Action.INSPECT_PDF
        "browser.back" -> SavedFlowReview.Action.BACK
        "browser.forward" -> SavedFlowReview.Action.FORWARD
        "browser.reload" -> SavedFlowReview.Action.RELOAD
        "browser.stop_loading" -> SavedFlowReview.Action.STOP_LOADING
        "browser.tabs.open" -> SavedFlowReview.Action.OPEN_TAB
        else -> return null
    }
    val addresses = arguments.filter { it.kind == SiteSkillArgumentKind.PUBLIC_ADDRESS }
    if (addresses.size > 1) return null
    val address = addresses.singleOrNull()?.let {
        it.public_address?.takeIf(::isReviewablePublicAddress) ?: return null
    }
    if (action == SavedFlowReview.Action.OPEN_PAGE && address == null) return null
    val targets = arguments.filter { it.kind == SiteSkillArgumentKind.SEMANTIC_TARGET }
    if (targets.size > 1) return null
    val target = targets.singleOrNull()?.let {
        val phrase = it.semantic_target?.phrase ?: return null
        SavedFlowReview.Target.entries.getOrNull(phrase.toInt()) ?: return null
    }
    if (verb in listOf("browser.dom.click", "browser.dom.focus", "browser.link.open", "browser.download.from_link") &&
        target == null
    ) return null
    val personArgument = arguments.firstOrNull { it.kind == SiteSkillArgumentKind.FROM_PERSON }
    val purpose = when {
        personArgument != null -> personArgument.purpose
        has_fill -> fill_purpose
        else -> null
    }
    return SavedFlowReview.Step(
        action = action,
        address = address,
        target = target,
        personPurpose = purpose?.let {
            when (it) {
                14u -> SavedFlowReview.PersonPurpose.IDENTITY_NUMBER
                15u -> SavedFlowReview.PersonPurpose.VERIFICATION
                16u -> SavedFlowReview.PersonPurpose.ONE_TIME_CODE
                else -> SavedFlowReview.PersonPurpose.FORM_DETAILS
            }
        },
    )
}

private fun isReviewablePublicAddress(address: String): Boolean = try {
    val uri = URI(address)
    uri.scheme == "https" && !uri.host.isNullOrBlank() &&
        uri.rawUserInfo == null && uri.rawQuery == null && uri.rawFragment == null
} catch (_: java.net.URISyntaxException) {
    false
}

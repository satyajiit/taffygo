// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
package org.chromium.taffy.host

import androidx.annotation.VisibleForTesting
import taffy.core_api.MAX_IDENTIFIER_BYTES
import taffy.core_api.MAX_SAVED_FLOW_QUERY_RESULTS
import taffy.core_api.MAX_SKILL_ARGUMENTS_PER_STEP
import taffy.core_api.MAX_SKILL_ID_BYTES
import taffy.core_api.MAX_SKILL_ORIGIN_BYTES
import taffy.core_api.MAX_SKILL_STEPS
import taffy.core_api.MAX_SKILL_TOOL_NAME_BYTES
import taffy.core_api.SavedFlowQueryAvailability
import taffy.core_api.SavedFlowQueryResult
import taffy.core_api.SiteSkillArgumentKind
import taffy.core_api.SiteSkillObservedArgument
import taffy.core_api.SiteSkillObservedStep
import taffy.core_api.SiteSkillProvenanceView
import taffy.core_api.SiteSkillSemanticTarget
import taffy.core_api.SiteSkillStatusView
import taffy.core_api.SiteSkillView

@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun projectSavedFlowQuery(
    result: org.chromium.taffy.core_api.mojom.SavedFlowQueryResult?,
    requestId: String,
): SavedFlowQueryResult? {
    result ?: return null
    val availability = SavedFlowQueryAvailability.fromWire(result.availability.toUInt()) ?: return null
    if (result.requestId != requestId || !requestId.bounded(MAX_IDENTIFIER_BYTES)) return null
    val raw = result.flows ?: return null
    if (raw.size > MAX_SAVED_FLOW_QUERY_RESULTS ||
        (availability != SavedFlowQueryAvailability.AVAILABLE && raw.isNotEmpty())
    ) return null
    val flows = raw.map { flow -> flow.review() ?: return null }
    if (flows.map { it.skill_id }.distinct().size != flows.size) return null
    return SavedFlowQueryResult(requestId, result.serviceGeneration.toULong(), availability, flows)
}

private fun org.chromium.taffy.core_api.mojom.SiteSkillView.review(): SiteSkillView? {
    val provenance = SiteSkillProvenanceView.fromWire(provenance.toUInt()) ?: return null
    val status = SiteSkillStatusView.fromWire(status.toUInt()) ?: return null
    if (!skillId.bounded(MAX_SKILL_ID_BYTES) || !origin.bounded(MAX_SKILL_ORIGIN_BYTES) ||
        activeVersion <= 0 || stepCount !in 1..MAX_SKILL_STEPS ||
        reviewedSteps == null || reviewedSteps.size != stepCount ||
        (recordedFromTaskId != null && !recordedFromTaskId.bounded(MAX_IDENTIFIER_BYTES))
    ) return null
    val steps = reviewedSteps.map { step ->
        if (!step.verb.bounded(MAX_SKILL_TOOL_NAME_BYTES) || step.arguments == null ||
            step.arguments.size > MAX_SKILL_ARGUMENTS_PER_STEP
        ) return null
        val arguments = step.arguments.map { argument ->
            val kind = SiteSkillArgumentKind.fromWire(argument.kind.toUInt()) ?: return null
            if ((kind == SiteSkillArgumentKind.PUBLIC_ADDRESS) != (argument.publicAddress != null) ||
                (kind == SiteSkillArgumentKind.SEMANTIC_TARGET) != (argument.semanticTarget != null)
            ) return null
            SiteSkillObservedArgument(
                argument.parameter.toUInt(), kind, argument.value.toULong(), argument.purpose.toUInt(),
                argument.publicAddress,
                argument.semanticTarget?.let { SiteSkillSemanticTarget(it.role.toUInt(), it.phrase.toUInt()) },
            )
        }
        SiteSkillObservedStep(step.verb, arguments, step.postcondition.toUInt(), step.hasFill, step.fillPurpose.toUInt())
    }
    return SiteSkillView(skillId, origin, provenance, status, activeVersion.toUInt(), stepCount.toUInt(),
        installedAtEpochMs.toULong(), updatedAtEpochMs.toULong(), recordedFromTaskId, steps)
}

private fun String?.bounded(maximum: Int): Boolean =
    this != null && isNotEmpty() && toByteArray(Charsets.UTF_8).size <= maximum

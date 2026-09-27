// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import com.taffygo.browser.ui.core.api.PartMemberRead
import org.chromium.taffy.core_api.mojom.CoreApiSubmissionStatus
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.CustomModelSpecView
import taffy.core_api.DetectedServerView
import taffy.core_api.PartMemberStatus
import taffy.core_api.TaskConsentPreview

@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun unavailableCoreStatus(
    generation: ULong,
    availability: CoreAvailability = CoreAvailability.UNAVAILABLE,
) = CoreStatus(
    availability = availability,
    generation = generation,
    active_tasks = emptyList(),
    auth_state = null,
    workspaces = emptyList(),
    workspace_export = null,
    // Absent rather than empty. An unavailable core has told this process
    // nothing about what is on the disk, and an empty delivery view would be
    // a claim it never made.
    asset_delivery = null,
    provider_roster = emptyList(),
    provider_probes = emptyList(),
    provider_models = emptyList(),
    library = taffy.core_api.LibraryViewState(
        availability = taffy.core_api.LibraryAvailability.UNAVAILABLE,
        revision = 0uL,
        entries = emptyList(),
        search = null,
        refresh_previews = emptyList(),
        refresh_results = emptyList(),
    ),
    library_export = null,
    memory = taffy.core_api.MemoryViewState(
        availability = taffy.core_api.MemoryAvailability.UNAVAILABLE,
        revision = 0uL,
        records = emptyList(),
        search = null,
    ),
    assistant_configuration = taffy.core_api.AssistantConfigurationView(
        revision = 0uL,
        disabled_abilities = emptyList(),
        preset = taffy.core_api.PersonalityPresetView.CAREFUL_RESEARCHER,
        pace = 0u,
        length = 1u,
        check_in = 0u,
    ),
    saved_sign_ins = taffy.core_api.SavedSignInsView(
        availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
        revision = 0uL,
        records = emptyList(),
    ),
    saved_details = taffy.core_api.SavedDetailsView(
        availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
        revision = 0uL,
        people = emptyList(),
    ),
    site_skills = emptyList(),
    // The fixed catalogue is supplied only by a ready core snapshot. This
    // process-local sentinel must not invent readiness for any built-in skill.
    builtin_skills = emptyList(),
    // Projection completeness is meaningful only for READY snapshots. Keep
    // the non-recovery shape here; availability remains the fail-closed gate.
    projection_mode = taffy.core_api.CoreStatusProjectionMode.COMPLETE,
    projection_omissions = emptyList(),
)

/**
 * The generated verdict for one member read, or the one that says the least.
 *
 * A wire value this build does not define fails closed to UNREADABLE rather
 * than to OK: bytes beside an unknown verdict cannot be trusted to be the
 * member, and treating them as one would put whatever arrived on the screen.
 */
internal fun partMemberRead(status: Int, bytes: ByteArray?) = PartMemberRead(
    status = PartMemberStatus.fromWire(status.toUInt()) ?: PartMemberStatus.UNREADABLE,
    bytes = bytes,
)

/**
 * The closed reason for one refused submission. A status the browser uses to
 * say a readable start was refused because of the person's tabs or windows
 * keeps its own reason here (decision 0231), because folding it into
 * INVALID_REQUEST is what put "Taffy could not read this request" over a
 * request nothing was wrong with.
 */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun submissionFailure(status: Int): CoreApiSubmissionException.Reason = when (status) {
    CoreApiSubmissionStatus.INVALID_REQUEST -> CoreApiSubmissionException.Reason.INVALID_REQUEST
    CoreApiSubmissionStatus.STALE_GENERATION -> CoreApiSubmissionException.Reason.STALE_GENERATION
    CoreApiSubmissionStatus.STALE_REVISION -> CoreApiSubmissionException.Reason.STALE_REVISION
    CoreApiSubmissionStatus.DEADLINE_EXCEEDED -> CoreApiSubmissionException.Reason.DEADLINE_EXCEEDED
    CoreApiSubmissionStatus.BACKPRESSURE -> CoreApiSubmissionException.Reason.BACKPRESSURE
    CoreApiSubmissionStatus.CORE_UNAVAILABLE -> CoreApiSubmissionException.Reason.CORE_UNAVAILABLE
    CoreApiSubmissionStatus.DUPLICATE -> CoreApiSubmissionException.Reason.DUPLICATE
    CoreApiSubmissionStatus.SOURCE_NOT_OPEN -> CoreApiSubmissionException.Reason.SOURCE_NOT_OPEN
    CoreApiSubmissionStatus.SOURCE_AMBIGUOUS -> CoreApiSubmissionException.Reason.SOURCE_AMBIGUOUS
    CoreApiSubmissionStatus.WINDOW_UNAVAILABLE -> CoreApiSubmissionException.Reason.WINDOW_UNAVAILABLE
    else -> CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION
}

/** One provider model spec in the generated Mojo vocabulary. */
/**
 * Every field of the consent, including the ones with nothing in them.
 *
 * The generated Java struct encodes a non-nullable array from whatever the
 * field holds, and a field left at its default is null: the encoder throws,
 * the dispatcher reads the throw as a dead transport, and every core-backed
 * surface reads "Taffy has not started yet" for the life of the process.
 * That is how `attached_stores` failed the first start after it was added,
 * so the array is always set here, empty or not.
 */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun TaskConsentPreview.toMojo(): org.chromium.taffy.core_api.mojom.TaskConsentPreview =
    org.chromium.taffy.core_api.mojom.TaskConsentPreview().apply {
        sourceHosts = source_hosts.toTypedArray()
        sourceDiscoveryEnabled = source_discovery_enabled
        newSourceCap = new_source_cap.toInt()
        providerRoute = provider_route.wire.toInt()
        attachedStores = attached_stores.map { it.wire.toInt() }.toIntArray()
    }

internal fun CustomModelSpecView.toMojo():
    org.chromium.taffy.core_api.mojom.CustomModelSpecView =
    org.chromium.taffy.core_api.mojom.CustomModelSpecView().apply {
        modelId = model_id
        displayName = display_name
        contextWindow = context_window.toInt()
        maxOutputTokens = max_output_tokens.toInt()
        reasoning = this@toMojo.reasoning
        toolCalling = tool_calling
    }

/** The detected provider runtime in the generated Mojo vocabulary. */
internal fun DetectedServerView.toMojo(): org.chromium.taffy.core_api.mojom.DetectedServerView =
    org.chromium.taffy.core_api.mojom.DetectedServerView().apply {
        serverKind = server_kind.wire.toInt()
    }

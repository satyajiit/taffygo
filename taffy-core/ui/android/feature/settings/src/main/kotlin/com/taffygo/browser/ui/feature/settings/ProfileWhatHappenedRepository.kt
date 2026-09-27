// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import java.time.Instant
import java.time.ZoneId
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import taffy.core_api.CoreAvailability

/**
 * Content-free latest-result projection over durable workspace outcomes and the
 * browser's measured filtering counter.
 *
 * The core does not publish page text, prompts, form values, or effect ids to
 * this adapter. Consequently none can accidentally enter this screen.
 */
internal class ProfileWhatHappenedRepository(
    core: CoreApiClient,
    workspaces: WorkspaceRepository,
    browser: BrowserRepository,
    scope: CoroutineScope,
    zoneId: ZoneId = ZoneId.systemDefault(),
) : WhatHappenedRepository {
    private val coreAvailability = core.status
        // Task deltas and provider/catalog publications cannot alter this
        // content-free screen unless the core's availability itself changes.
        .map { it.availability }
        .distinctUntilChanged()

    override val snapshot: StateFlow<WhatHappenedRepository.Snapshot> = combine(
        coreAvailability,
        workspaces.availability,
        workspaces.workspaces,
        browser.filtering,
    ) { availability, workspaceAvailability, currentWorkspaces, filtering ->
        projectWhatHappenedSnapshot(
            availability = availability,
            workspaceAvailability = workspaceAvailability,
            workspaces = currentWorkspaces,
            filtering = filtering,
            zoneId = zoneId,
        )
    }.stateIn(
        scope = scope,
        started = SharingStarted.Eagerly,
        initialValue = projectWhatHappenedSnapshot(
            availability = core.status.value.availability,
            workspaceAvailability = workspaces.availability.value,
            workspaces = workspaces.workspaces.value,
            filtering = browser.filtering.value,
            zoneId = zoneId,
        ),
    )
}

/** Deterministic, bounded projection of facts already published for this profile. */
internal fun projectWhatHappenedSnapshot(
    availability: CoreAvailability,
    workspaceAvailability: WorkspaceRepository.Availability = WorkspaceRepository.Availability.READY,
    workspaces: List<Workspace>,
    filtering: FilteringSettings,
    zoneId: ZoneId,
): WhatHappenedRepository.Snapshot = when (availability) {
    CoreAvailability.STARTING -> WhatHappenedRepository.Snapshot(
        availability = YouSurfaceAvailability.LOADING,
    )
    CoreAvailability.UNAVAILABLE,
    CoreAvailability.CIRCUIT_OPEN,
    -> WhatHappenedRepository.Snapshot(
        availability = YouSurfaceAvailability.UNAVAILABLE,
    )
    CoreAvailability.READY -> when (workspaceAvailability) {
        WorkspaceRepository.Availability.LOADING -> WhatHappenedRepository.Snapshot(
            availability = YouSurfaceAvailability.LOADING,
        )
        WorkspaceRepository.Availability.UNAVAILABLE -> WhatHappenedRepository.Snapshot(
            availability = YouSurfaceAvailability.UNAVAILABLE,
        )
        WorkspaceRepository.Availability.READY -> {
            val events = workspaces.asSequence()
                .filter { it.lastUpdatedEpochMillis >= 0L }
                .filter { it.state.isFinal }
                .sortedWith(
                    compareByDescending<Workspace> { it.lastUpdatedEpochMillis }
                        .thenBy { it.id.value },
                )
                .take(MAX_WHAT_HAPPENED_EVENTS)
                .mapNotNull { workspace -> workspace.toActivityEventOrNull(zoneId) }
                .toList()
            WhatHappenedRepository.Snapshot(
                availability = YouSurfaceAvailability.READY,
                events = events,
                blockedRequestsThisWeek = filtering.blockedThisWeek?.coerceAtLeast(0L),
            )
        }
    }
}

private fun Workspace.toActivityEventOrNull(
    zoneId: ZoneId,
): WhatHappenedRepository.Event? {
    val day = runCatching {
        Instant.ofEpochMilli(lastUpdatedEpochMillis).atZone(zoneId).toLocalDate().toEpochDay()
    }.getOrNull() ?: return null
    val kind = when (state) {
        TaskDisplayState.STOPPED -> WhatHappenedRepository.Kind.TASK_STOPPED
        TaskDisplayState.FAILED -> WhatHappenedRepository.Kind.TASK_FAILED
        else -> WhatHappenedRepository.Kind.WORKSPACE_RESULT
    }
    return WhatHappenedRepository.Event(
        id = "workspace-${id.value}-$lastUpdatedEpochMillis",
        kind = kind,
        epochMillis = lastUpdatedEpochMillis,
        epochDay = day,
        sourceCount = sources.size,
        workspaceId = id.value,
        workspaceGone = false,
    )
}

private const val MAX_WHAT_HAPPENED_EVENTS = 500

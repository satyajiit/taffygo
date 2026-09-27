// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * The latest terminal snapshot for each task workspace, plus the browser's
 * current weekly blocking total. This is deliberately not an event journal.
 */
interface WhatHappenedRepository {

    /** Day-grouped events, or an honest unavailable. */
    val snapshot: StateFlow<Snapshot>

    /** One complete answer from the port. */
    data class Snapshot(
        val availability: YouSurfaceAvailability,
        val events: List<Event> = emptyList(),
        val blockedRequestsThisWeek: Long? = null,
    )

    /** One content-free row. */
    data class Event(
        val id: String,
        val kind: Kind,
        val epochMillis: Long,
        val epochDay: Long,
        val sourceCount: Int = 0,
        val workspaceId: String? = null,
        val workspaceGone: Boolean = false,
    )

    /** Terminal workspace states this projection can actually prove. */
    enum class Kind {
        WORKSPACE_RESULT,
        TASK_STOPPED,
        TASK_FAILED,
    }
}

/** Fail-closed fallback for previews or hosts without the profile projection. */
internal class UnavailableWhatHappenedRepository : WhatHappenedRepository {
    override val snapshot: StateFlow<WhatHappenedRepository.Snapshot> =
        MutableStateFlow(
            WhatHappenedRepository.Snapshot(
                availability = YouSurfaceAvailability.UNAVAILABLE,
            ),
        ).asStateFlow()
}

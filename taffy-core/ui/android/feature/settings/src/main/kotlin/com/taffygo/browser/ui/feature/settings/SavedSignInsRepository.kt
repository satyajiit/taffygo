// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.api.CoreApiClient
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.SavedDataAvailability
import taffy.core_api.SavedSignInView

/** Chromium-owned saved sign-in metadata. Password material is unrepresentable here. */
interface SavedSignInsRepository {

    /** One bounded metadata snapshot, or an honest unavailable. */
    val snapshot: StateFlow<Snapshot>

    /** Forget [id] at the exact revision shown to the person. */
    suspend fun delete(id: String)

    data class Snapshot(
        val availability: YouSurfaceAvailability,
        val revision: ULong = 0uL,
        val records: List<Record> = emptyList(),
    )

    /** The username is sensitive metadata and is redacted from diagnostic text. */
    data class Record(
        val id: String,
        val site: String,
        val username: String,
        val lastUsedEpochMillis: Long,
    ) {
        override fun toString(): String =
            "Record(id=$id, site=$site, username=<redacted>, lastUsedEpochMillis=$lastUsedEpochMillis)"
    }
}

/** Profile projection and mutation through the generated Core API only. */
internal class CoreSavedSignInsRepository(
    private val core: CoreApiClient,
    scope: CoroutineScope,
) : SavedSignInsRepository {
    override val snapshot: StateFlow<SavedSignInsRepository.Snapshot> = core.status
        .distinctUntilChangedBy(::savedSignInsSnapshotVersion)
        .map(::projectSavedSignInsSnapshot)
        .stateIn(
            scope = scope,
            started = SharingStarted.Eagerly,
            initialValue = projectSavedSignInsSnapshot(core.status.value),
        )

    override suspend fun delete(id: String) {
        val status = core.status.value
        val saved = status.saved_sign_ins
        if (status.availability != CoreAvailability.READY ||
            saved.availability != SavedDataAvailability.READY ||
            saved.records.none { it.id == id }
        ) {
            return
        }
        core.deleteSavedSignIn(id, saved.revision)
    }
}

internal data class SavedSignInsSnapshotVersion(
    val coreAvailability: CoreAvailability,
    val availability: SavedDataAvailability,
    val revision: ULong,
)

internal fun savedSignInsSnapshotVersion(status: CoreStatus) = SavedSignInsSnapshotVersion(
    coreAvailability = status.availability,
    availability = status.saved_sign_ins.availability,
    revision = status.saved_sign_ins.revision,
)

internal fun projectSavedSignInsSnapshot(status: CoreStatus): SavedSignInsRepository.Snapshot =
    when (status.availability) {
        CoreAvailability.STARTING ->
            SavedSignInsRepository.Snapshot(YouSurfaceAvailability.LOADING)
        CoreAvailability.UNAVAILABLE,
        CoreAvailability.CIRCUIT_OPEN,
        -> SavedSignInsRepository.Snapshot(YouSurfaceAvailability.UNAVAILABLE)
        CoreAvailability.READY -> when (status.saved_sign_ins.availability) {
            SavedDataAvailability.LOADING ->
                SavedSignInsRepository.Snapshot(YouSurfaceAvailability.LOADING)
            SavedDataAvailability.UNAVAILABLE ->
                SavedSignInsRepository.Snapshot(YouSurfaceAvailability.UNAVAILABLE)
            SavedDataAvailability.READY -> SavedSignInsRepository.Snapshot(
                availability = YouSurfaceAvailability.READY,
                revision = status.saved_sign_ins.revision,
                records = status.saved_sign_ins.records.map(::projectSavedSignIn),
            )
        }
    }

private fun projectSavedSignIn(record: SavedSignInView): SavedSignInsRepository.Record =
    SavedSignInsRepository.Record(
        id = record.id,
        site = record.site,
        username = record.username,
        lastUsedEpochMillis = record.last_used_epoch_ms.toLong(),
    )

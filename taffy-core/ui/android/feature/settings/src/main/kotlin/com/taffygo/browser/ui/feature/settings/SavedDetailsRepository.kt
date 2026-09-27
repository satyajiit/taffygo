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
import taffy.core_api.SavedDetailView

/** Name, address, email, and phone held by Chromium Autofill, never Taffy storage. */
interface SavedDetailsRepository {

    val snapshot: StateFlow<Snapshot>

    /** Insert or replace one person at the exact revision shown. */
    suspend fun upsert(person: Person)

    /** Forget [id] at the exact revision shown. */
    suspend fun delete(id: String)

    data class Snapshot(
        val availability: YouSurfaceAvailability,
        val revision: ULong = 0uL,
        val people: List<Person> = emptyList(),
    )

    /** One address profile. There is deliberately no national-id field. */
    data class Person(
        val id: String,
        val givenName: String = "",
        val familyName: String = "",
        val email: String = "",
        val phone: String = "",
        val address: String = "",
        val postcode: String = "",
        val country: String = "",
    )
}

/** Profile projection and exact-revision mutations through the generated Core API. */
internal class CoreSavedDetailsRepository(
    private val core: CoreApiClient,
    scope: CoroutineScope,
) : SavedDetailsRepository {
    override val snapshot: StateFlow<SavedDetailsRepository.Snapshot> = core.status
        .distinctUntilChangedBy(::savedDetailsSnapshotVersion)
        .map(::projectSavedDetailsSnapshot)
        .stateIn(
            scope = scope,
            started = SharingStarted.Eagerly,
            initialValue = projectSavedDetailsSnapshot(core.status.value),
        )

    override suspend fun upsert(person: SavedDetailsRepository.Person) {
        val status = core.status.value
        val saved = status.saved_details
        if (status.availability != CoreAvailability.READY ||
            saved.availability != SavedDataAvailability.READY
        ) {
            return
        }
        val detailId = person.id.takeIf { candidate ->
            saved.people.any { it.id == candidate }
        }
        core.upsertSavedDetail(
            expectedRevision = saved.revision,
            detailId = detailId,
            givenName = person.givenName,
            familyName = person.familyName,
            email = person.email,
            phone = person.phone,
            address = person.address,
            postcode = person.postcode,
            country = person.country,
        )
    }

    override suspend fun delete(id: String) {
        val status = core.status.value
        val saved = status.saved_details
        if (status.availability != CoreAvailability.READY ||
            saved.availability != SavedDataAvailability.READY ||
            saved.people.none { it.id == id }
        ) {
            return
        }
        core.deleteSavedDetail(id, saved.revision)
    }
}

internal data class SavedDetailsSnapshotVersion(
    val coreAvailability: CoreAvailability,
    val availability: SavedDataAvailability,
    val revision: ULong,
)

internal fun savedDetailsSnapshotVersion(status: CoreStatus) = SavedDetailsSnapshotVersion(
    coreAvailability = status.availability,
    availability = status.saved_details.availability,
    revision = status.saved_details.revision,
)

internal fun projectSavedDetailsSnapshot(status: CoreStatus): SavedDetailsRepository.Snapshot =
    when (status.availability) {
        CoreAvailability.STARTING ->
            SavedDetailsRepository.Snapshot(YouSurfaceAvailability.LOADING)
        CoreAvailability.UNAVAILABLE,
        CoreAvailability.CIRCUIT_OPEN,
        -> SavedDetailsRepository.Snapshot(YouSurfaceAvailability.UNAVAILABLE)
        CoreAvailability.READY -> when (status.saved_details.availability) {
            SavedDataAvailability.LOADING ->
                SavedDetailsRepository.Snapshot(YouSurfaceAvailability.LOADING)
            SavedDataAvailability.UNAVAILABLE ->
                SavedDetailsRepository.Snapshot(YouSurfaceAvailability.UNAVAILABLE)
            SavedDataAvailability.READY -> SavedDetailsRepository.Snapshot(
                availability = YouSurfaceAvailability.READY,
                revision = status.saved_details.revision,
                people = status.saved_details.people.map(::projectSavedDetail),
            )
        }
    }

private fun projectSavedDetail(detail: SavedDetailView): SavedDetailsRepository.Person =
    SavedDetailsRepository.Person(
        id = detail.id,
        givenName = detail.given_name,
        familyName = detail.family_name,
        email = detail.email,
        phone = detail.phone,
        address = detail.address,
        postcode = detail.postcode,
        country = detail.country,
    )

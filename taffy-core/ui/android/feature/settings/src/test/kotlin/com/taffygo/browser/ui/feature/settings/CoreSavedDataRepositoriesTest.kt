// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.AssistantConfigurationView
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.PersonalityPresetView
import taffy.core_api.SavedDataAvailability
import taffy.core_api.SavedDetailView
import taffy.core_api.SavedDetailsView
import taffy.core_api.SavedSignInView
import taffy.core_api.SavedSignInsView

@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreSavedDataRepositoriesTest {

    @Test
    fun `ready snapshots project bounded metadata without a password shape`() = runTest {
        val core = RecordingAssistantConfigurationCoreApiClient(savedDataStatus())
        val signIns = CoreSavedSignInsRepository(core, backgroundScope)
        val details = CoreSavedDetailsRepository(core, backgroundScope)
        advanceUntilIdle()

        assertEquals(11uL, signIns.snapshot.value.revision)
        assertEquals("person@example.test", signIns.snapshot.value.records.single().username)
        assertEquals(12uL, details.snapshot.value.revision)
        assertEquals("Asha", details.snapshot.value.people.single().givenName)
        assertTrue(
            SavedSignInsRepository.Record::class.java.declaredFields
                .none { it.name.contains("password", ignoreCase = true) },
        )
        val diagnostic = signIns.snapshot.value.records.single().toString()
        assertFalse(diagnostic.contains("person@example.test"))
        assertTrue(diagnostic.contains("username=<redacted>"))
    }

    @Test
    fun `mutations use only current opaque ids and exact published revisions`() = runTest {
        val core = RecordingAssistantConfigurationCoreApiClient(savedDataStatus())
        val signIns = CoreSavedSignInsRepository(core, backgroundScope)
        val details = CoreSavedDetailsRepository(core, backgroundScope)

        signIns.delete("saved-sign-in-1")
        details.delete("saved-detail-1")
        details.upsert(details.snapshot.value.people.single().copy(phone = "555 0200"))
        details.upsert(details.snapshot.value.people.single().copy(id = "forged-id"))

        assertEquals(
            RecordingAssistantConfigurationCoreApiClient.SavedDataDeletion(
                "saved-sign-in-1",
                11uL,
            ),
            core.savedSignInDeletions.single(),
        )
        assertEquals(
            RecordingAssistantConfigurationCoreApiClient.SavedDataDeletion(
                "saved-detail-1",
                12uL,
            ),
            core.savedDetailDeletions.single(),
        )
        assertEquals("saved-detail-1", core.savedDetailUpserts.first().detailId)
        assertEquals(12uL, core.savedDetailUpserts.first().expectedRevision)
        assertNull(core.savedDetailUpserts.last().detailId)
    }

    @Test
    fun `unavailable snapshots publish no rows and authorize no mutation`() = runTest {
        val core = RecordingAssistantConfigurationCoreApiClient(
            savedDataStatus(savedAvailability = SavedDataAvailability.UNAVAILABLE),
        )
        val signIns = CoreSavedSignInsRepository(core, backgroundScope)
        val details = CoreSavedDetailsRepository(core, backgroundScope)
        advanceUntilIdle()

        assertEquals(YouSurfaceAvailability.UNAVAILABLE, signIns.snapshot.value.availability)
        assertEquals(YouSurfaceAvailability.UNAVAILABLE, details.snapshot.value.availability)
        assertTrue(signIns.snapshot.value.records.isEmpty())
        assertTrue(details.snapshot.value.people.isEmpty())
        signIns.delete("saved-sign-in-1")
        details.delete("saved-detail-1")
        details.upsert(savedPerson())
        assertTrue(core.savedSignInDeletions.isEmpty())
        assertTrue(core.savedDetailDeletions.isEmpty())
        assertTrue(core.savedDetailUpserts.isEmpty())
    }

    @Test
    fun `unrelated status updates preserve each Saved Data revision key`() {
        val baseline = savedDataStatus()
        val unrelated = baseline.copy(generation = 9uL)
        assertEquals(
            savedSignInsSnapshotVersion(baseline),
            savedSignInsSnapshotVersion(unrelated),
        )
        assertEquals(
            savedDetailsSnapshotVersion(baseline),
            savedDetailsSnapshotVersion(unrelated),
        )
        assertNotEquals(
            savedSignInsSnapshotVersion(baseline),
            savedSignInsSnapshotVersion(
                baseline.copy(saved_sign_ins = baseline.saved_sign_ins.copy(revision = 13uL)),
            ),
        )
        assertNotEquals(
            savedDetailsSnapshotVersion(baseline),
            savedDetailsSnapshotVersion(
                baseline.copy(saved_details = baseline.saved_details.copy(revision = 14uL)),
            ),
        )
    }
}

private fun savedDataStatus(
    coreAvailability: CoreAvailability = CoreAvailability.READY,
    savedAvailability: SavedDataAvailability = SavedDataAvailability.READY,
) = CoreStatus(
    availability = coreAvailability,
    generation = 3uL,
    active_tasks = emptyList(),
    auth_state = null,
    workspaces = emptyList(),
    workspace_export = null,
    asset_delivery = null,
    provider_roster = emptyList(),
    provider_probes = emptyList(),
    provider_models = emptyList(),
    assistant_configuration = AssistantConfigurationView(
        revision = 0uL,
        disabled_abilities = emptyList(),
        preset = PersonalityPresetView.CAREFUL_RESEARCHER,
        pace = 0u,
        length = 1u,
        check_in = 0u,
    ),
    library = taffy.core_api.LibraryViewState(
        availability = taffy.core_api.LibraryAvailability.AVAILABLE,
        revision = 0uL,
        entries = emptyList(),
        search = null,
        refresh_previews = emptyList(),
        refresh_results = emptyList(),
    ),
    library_export = null,
    memory = taffy.core_api.MemoryViewState(
        availability = taffy.core_api.MemoryAvailability.AVAILABLE,
        revision = 0uL,
        records = emptyList(),
        search = null,
    ),
    saved_sign_ins = SavedSignInsView(
        availability = savedAvailability,
        revision = if (savedAvailability == SavedDataAvailability.READY) 11uL else 0uL,
        records = if (savedAvailability == SavedDataAvailability.READY) {
            listOf(
                SavedSignInView(
                    id = "saved-sign-in-1",
                    site = "example.test",
                    username = "person@example.test",
                    last_used_epoch_ms = 1_780_000_000_000uL,
                ),
            )
        } else {
            emptyList()
        },
    ),
    saved_details = SavedDetailsView(
        availability = savedAvailability,
        revision = if (savedAvailability == SavedDataAvailability.READY) 12uL else 0uL,
        people = if (savedAvailability == SavedDataAvailability.READY) {
            listOf(savedDetailView())
        } else {
            emptyList()
        },
    ),
    site_skills = emptyList(),
    builtin_skills = emptyList(),
    projection_mode = taffy.core_api.CoreStatusProjectionMode.COMPLETE,
    projection_omissions = emptyList(),
)

private fun savedDetailView() = SavedDetailView(
    id = "saved-detail-1",
    given_name = "Asha",
    family_name = "Rao",
    email = "asha@example.test",
    phone = "555 0100",
    address = "12 First Street",
    postcode = "411001",
    country = "IN",
)

private fun savedPerson() = SavedDetailsRepository.Person(
    id = "saved-detail-1",
    givenName = "Asha",
)

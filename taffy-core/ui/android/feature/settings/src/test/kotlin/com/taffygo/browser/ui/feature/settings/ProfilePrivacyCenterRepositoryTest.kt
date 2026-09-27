// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.AssistantConfigurationView
import taffy.core_api.CatalogLayerView
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.CoreStatusProjectionMode
import taffy.core_api.LibraryAvailability
import taffy.core_api.LibraryEntryView
import taffy.core_api.LibraryViewState
import taffy.core_api.MAX_LIBRARY_ENTRIES
import taffy.core_api.MAX_MEMORY_RECORDS
import taffy.core_api.MAX_PROVIDER_ROSTER_ENTRIES
import taffy.core_api.MAX_WORKSPACES
import taffy.core_api.MemoryAvailability
import taffy.core_api.MemoryRecordView
import taffy.core_api.MemoryScopeKind
import taffy.core_api.MemorySensitivity
import taffy.core_api.MemorySourceKind
import taffy.core_api.MemoryViewState
import taffy.core_api.PersonalityPresetView
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderOriginView
import taffy.core_api.ProviderRosterEntry
import taffy.core_api.SavedDataAvailability
import taffy.core_api.SavedDetailsView
import taffy.core_api.SavedSignInsView
import taffy.core_api.StoredCredentialView
import taffy.core_api.TaskTemplateId
import taffy.core_api.WorkspacePhase
import taffy.core_api.WorkspaceViewState

@OptIn(ExperimentalCoroutinesApi::class)
class ProfilePrivacyCenterRepositoryTest {
    @Test
    fun `ready state projects only exact content-free totals`() {
        val canary = "PRIVATE-CONTENT-CANARY"
        val status = status(
            workspaces = listOf(
                workspace("saved-one-$canary", saved = true),
                workspace("running-$canary", saved = false),
                workspace("saved-two-$canary", saved = true),
            ),
            libraryEntries = List(3) { libraryEntry("$canary-$it") },
            memoryRecords = List(4) { memoryRecord("$canary-$it") },
            providers = listOf(
                provider("$canary-one", connected = true),
                provider("$canary-two", connected = false),
                provider("$canary-three", connected = true),
            ),
        )
        val snapshot = projectPrivacyCenterSnapshot(
            status = status,
            downloads = List(5) { download("$canary-$it") },
            siteSettings = SiteSettingsRepository.Snapshot.Ready(
                defaults = emptyList(),
                sites = List(6) { site("$canary-$it") },
            ),
        )

        assertEquals(PrivacyDataCount.Known(2), snapshot.savedWorkspaces)
        assertEquals(PrivacyDataCount.Known(3), snapshot.libraryItems)
        assertEquals(PrivacyDataCount.Known(4), snapshot.memoryItems)
        assertEquals(PrivacyDataCount.Known(2), snapshot.connectedProviders)
        assertEquals(PrivacyDataCount.Known(5), snapshot.recentDownloads)
        assertEquals(PrivacyDataCount.Known(6), snapshot.changedSites)
        assertFalse(snapshot.toString().contains(canary))
        assertFalse(snapshot.exportAvailable)
        assertFalse(snapshot.deleteAvailable)
    }

    @Test
    fun `starting and unavailable core never turn absent facts into zero`() {
        val starting = projectPrivacyCenterSnapshot(
            status(CoreAvailability.STARTING),
            emptyList(),
            SiteSettingsRepository.Snapshot.Loading,
        )
        val unavailable = projectPrivacyCenterSnapshot(
            status(CoreAvailability.CIRCUIT_OPEN),
            emptyList(),
            SiteSettingsRepository.Snapshot.Unavailable,
        )

        assertEquals(PrivacyDataCount.Loading, starting.savedWorkspaces)
        assertEquals(PrivacyDataCount.Loading, starting.libraryItems)
        assertEquals(PrivacyDataCount.Known(0), starting.recentDownloads)
        assertEquals(PrivacyDataCount.Loading, starting.changedSites)
        assertEquals(PrivacyDataCount.Unavailable, unavailable.savedWorkspaces)
        assertEquals(PrivacyDataCount.Unavailable, unavailable.libraryItems)
        assertEquals(PrivacyDataCount.Unavailable, unavailable.memoryItems)
        assertEquals(PrivacyDataCount.Unavailable, unavailable.connectedProviders)
        assertEquals(PrivacyDataCount.Unavailable, unavailable.changedSites)
    }

    @Test
    fun `recovery projection makes all four core counts unavailable`() {
        val snapshot = projectPrivacyCenterSnapshot(
            status(
                workspaces = listOf(workspace("saved", saved = true)),
                libraryEntries = listOf(libraryEntry("library")),
                memoryRecords = listOf(memoryRecord("memory")),
                providers = listOf(provider("provider", connected = true)),
            ).copy(projection_mode = CoreStatusProjectionMode.RECOVERY_REQUIRED),
            emptyList(),
            SiteSettingsRepository.Snapshot.Ready(emptyList(), emptyList()),
        )

        assertEquals(PrivacyDataCount.Unavailable, snapshot.savedWorkspaces)
        assertEquals(PrivacyDataCount.Unavailable, snapshot.libraryItems)
        assertEquals(PrivacyDataCount.Unavailable, snapshot.memoryItems)
        assertEquals(PrivacyDataCount.Unavailable, snapshot.connectedProviders)
    }

    @Test
    fun `private core stores stay unavailable instead of claiming an empty store`() {
        val status = status().copy(
            library = LibraryViewState(
                availability = LibraryAvailability.PRIVATE_PROFILE,
                revision = 0uL,
                entries = emptyList(),
                search = null,
                refresh_previews = emptyList(),
                refresh_results = emptyList(),
            ),
            memory = MemoryViewState(
                availability = MemoryAvailability.PRIVATE_PROFILE,
                revision = 0uL,
                records = emptyList(),
                search = null,
            ),
        )

        val snapshot = projectPrivacyCenterSnapshot(
            status,
            emptyList(),
            SiteSettingsRepository.Snapshot.Ready(emptyList(), emptyList()),
        )

        assertEquals(PrivacyDataCount.Unavailable, snapshot.libraryItems)
        assertEquals(PrivacyDataCount.Unavailable, snapshot.memoryItems)
        assertEquals(PrivacyDataCount.Known(0), snapshot.savedWorkspaces)
    }

    @Test
    fun `malformed core collections fail closed at their contract bounds`() {
        val snapshot = projectPrivacyCenterSnapshot(
            status(
                workspaces = List(MAX_WORKSPACES + 1) { workspace("w-$it", saved = true) },
                libraryEntries = List(MAX_LIBRARY_ENTRIES + 1) { libraryEntry("l-$it") },
                memoryRecords = List(MAX_MEMORY_RECORDS + 1) { memoryRecord("m-$it") },
                providers = List(MAX_PROVIDER_ROSTER_ENTRIES + 1) {
                    provider("p-$it", connected = true)
                },
            ),
            emptyList(),
            SiteSettingsRepository.Snapshot.Ready(emptyList(), emptyList()),
        )

        assertEquals(PrivacyDataCount.Unavailable, snapshot.savedWorkspaces)
        assertEquals(PrivacyDataCount.Unavailable, snapshot.libraryItems)
        assertEquals(PrivacyDataCount.Unavailable, snapshot.memoryItems)
        assertEquals(PrivacyDataCount.Unavailable, snapshot.connectedProviders)
    }

    @Test
    fun `browser collections become bounded lower limits`() {
        val snapshot = projectPrivacyCenterSnapshot(
            status(),
            List(257) { download("download-$it") },
            SiteSettingsRepository.Snapshot.Ready(
                defaults = emptyList(),
                sites = List(4_097) { site("site-$it") },
            ),
        )

        assertEquals(PrivacyDataCount.Known(256, isLowerBound = true), snapshot.recentDownloads)
        assertEquals(PrivacyDataCount.Known(4_096, isLowerBound = true), snapshot.changedSites)
    }

    @Test
    fun `repository republishes source changes and refuses incomplete whole profile actions`() =
        runTest {
            val core = MutableStateFlow(status(CoreAvailability.STARTING))
            val downloads = MutableStateFlow(emptyList<DownloadRecord>())
            val sites = MutableStateFlow<SiteSettingsRepository.Snapshot>(
                SiteSettingsRepository.Snapshot.Loading,
            )
            val dataControl = FakeProfileDataControl()
            val repository = ProfilePrivacyCenterRepository(
                coreStatus = core,
                downloads = downloads,
                siteSettings = sites,
                dataControl = dataControl,
                scope = backgroundScope,
            )

            core.value = status(workspaces = listOf(workspace("saved", saved = true)))
            downloads.value = listOf(download("complete"))
            sites.value = SiteSettingsRepository.Snapshot.Ready(
                defaults = emptyList(),
                sites = listOf(site("changed.test")),
            )
            runCurrent()

            assertEquals(PrivacyDataCount.Known(1), repository.snapshot.value.savedWorkspaces)
            assertEquals(PrivacyDataCount.Known(1), repository.snapshot.value.recentDownloads)
            assertEquals(PrivacyDataCount.Known(1), repository.snapshot.value.changedSites)
            assertEquals(
                ProfileDataControl.ExportResult.UNAVAILABLE,
                repository.exportEverything { true },
            )
            assertEquals(
                ProfileDataControl.DeletionResult.UNAVAILABLE,
                repository.deleteEverything(),
            )

            dataControl.state.value = ProfileDataControl.Availability(true, true)
            runCurrent()
            assertTrue(repository.snapshot.value.exportAvailable)
            assertTrue(repository.snapshot.value.deleteAvailable)
        }

    @Test
    fun `known count rejects values outside the content-free envelope`() {
        assertTrue(runCatching { PrivacyDataCount.Known(-1) }.isFailure)
        assertTrue(runCatching { PrivacyDataCount.Known(4_097) }.isFailure)
        assertTrue(runCatching { PrivacyDataCount.Known(0, isLowerBound = true) }.isFailure)
    }
}

private class FakeProfileDataControl : ProfileDataControl {
    val state = MutableStateFlow(ProfileDataControl.Availability(false, false))
    override val availability = state

    override suspend fun export(write: suspend (ByteArray) -> Boolean) =
        ProfileDataControl.ExportResult.UNAVAILABLE

    override suspend fun deleteApplicationData() =
        ProfileDataControl.DeletionResult.UNAVAILABLE
}

private fun status(
    availability: CoreAvailability = CoreAvailability.READY,
    workspaces: List<WorkspaceViewState> = emptyList(),
    libraryEntries: List<LibraryEntryView> = emptyList(),
    memoryRecords: List<MemoryRecordView> = emptyList(),
    providers: List<ProviderRosterEntry> = emptyList(),
) = CoreStatus(
    availability = availability,
    generation = 1uL,
    active_tasks = emptyList(),
    auth_state = null,
    workspaces = workspaces,
    workspace_export = null,
    asset_delivery = null,
    provider_roster = providers,
    provider_probes = emptyList(),
    provider_models = emptyList(),
    assistant_configuration = AssistantConfigurationView(
        revision = 0uL,
        disabled_abilities = emptyList(),
        preset = PersonalityPresetView.CAREFUL_RESEARCHER,
        pace = 0u,
        length = 0u,
        check_in = 0u,
    ),
    library = LibraryViewState(
        availability = LibraryAvailability.AVAILABLE,
        revision = 0uL,
        entries = libraryEntries,
        search = null,
        refresh_previews = emptyList(),
        refresh_results = emptyList(),
    ),
    library_export = null,
    memory = MemoryViewState(
        availability = MemoryAvailability.AVAILABLE,
        revision = 0uL,
        records = memoryRecords,
        search = null,
    ),
    saved_sign_ins = SavedSignInsView(
        availability = SavedDataAvailability.READY,
        revision = 0uL,
        records = emptyList(),
    ),
    saved_details = SavedDetailsView(
        availability = SavedDataAvailability.READY,
        revision = 0uL,
        people = emptyList(),
    ),
    site_skills = emptyList(),
    builtin_skills = emptyList(),
    projection_mode = taffy.core_api.CoreStatusProjectionMode.COMPLETE,
    projection_omissions = emptyList(),
)

private fun workspace(id: String, saved: Boolean) = WorkspaceViewState(
    workspace_id = id,
    revision = 1uL,
    goal = id,
    phase = WorkspacePhase.DONE,
    last_updated_epoch_ms = 1uL,
    template_id = TaskTemplateId.SUMMARIZE_EVIDENCE,
    sources = emptyList(),
    facts = emptyList(),
    saved = saved,
    display_name = id,
    deletion_preview = null,
)

private fun libraryEntry(id: String) = LibraryEntryView(
    entry_id = id,
    revision = 1uL,
    collection_id = id,
    collection_name = id,
    source_workspace_id = id,
    source_workspace_revision = 1uL,
    source_fact_id = id,
    field = id,
    original_value = id,
    correction = null,
    kind = taffy.core_api.WorkspaceFactKind.USER_ENTERED,
    sources = emptyList(),
    captured_at_epoch_ms = 1uL,
    last_checked_epoch_ms = 1uL,
    has_conflict = false,
)

private fun memoryRecord(id: String) = MemoryRecordView(
    memory_id = id,
    revision = 1uL,
    statement = id,
    source_kind = MemorySourceKind.YOU_WROTE,
    source_task_id = null,
    source_workspace = null,
    scope_kind = MemoryScopeKind.ALL_TASKS,
    scope_workspace = null,
    sensitivity = MemorySensitivity.STANDARD,
    created_at_epoch_ms = 1uL,
    updated_at_epoch_ms = 1uL,
    reviewed_at_epoch_ms = 1uL,
    expires_at_epoch_ms = 0uL,
)

private fun provider(id: String, connected: Boolean) = ProviderRosterEntry(
    provider_id = id,
    display_name = id,
    origin = ProviderOriginView.CATALOG,
    auth_methods = listOf(ProviderAuthMethodView.API_KEY),
    stored = if (connected) {
        StoredCredentialView(
            auth_method = ProviderAuthMethodView.API_KEY,
            state = ProviderCredentialStateView.USABLE,
            subscription_backed = false,
            account_label = id,
            plan_label = null,
        )
    } else {
        null
    },
    signing_in = false,
    enabled = connected,
    endpoint_host = id,
    configurable = false,
    endpoint_changed = false,
    catalog_layer = CatalogLayerView.EMBEDDED_BASELINE,
    selected_model_id = null,
    thinking = null,
    presentation = null,
    endpoint_base = null,
    last_refusal = null,
    model_count = 0u,
    refused_endpoint_host = null,
    subscription = false,
)

private fun download(id: String) = DownloadRecord(
    id = DownloadId(id),
    fileName = id,
    host = id,
    totalBytes = 1L,
    downloadedBytes = 1L,
    state = DownloadState.COMPLETE,
    allowedActions = emptySet(),
)

private fun site(host: String) = SiteSettingsRepository.Entry(
    host = host,
    changedPermissionCount = 1,
)

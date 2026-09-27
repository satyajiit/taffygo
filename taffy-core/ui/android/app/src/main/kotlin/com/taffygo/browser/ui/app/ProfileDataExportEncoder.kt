// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.feature.browsing.BookmarksSnapshot
import com.taffygo.browser.ui.feature.browsing.HistorySnapshot
import com.taffygo.browser.ui.feature.settings.SiteSettingsRepository
import com.taffygo.browser.ui.feature.settings.TimeOnSitesRepository
import com.taffygo.browser.ui.feature.settings.YouSurfaceAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.LibraryAvailability
import taffy.core_api.MAX_FACT_SOURCES
import taffy.core_api.MAX_LIBRARY_ENTRIES
import taffy.core_api.MAX_LIBRARY_SOURCES
import taffy.core_api.MAX_MEMORY_RECORDS
import taffy.core_api.MAX_PROVIDER_ROSTER_ENTRIES
import taffy.core_api.MAX_SAVED_DETAILS
import taffy.core_api.MAX_SAVED_SIGN_INS
import taffy.core_api.MAX_SITE_SKILLS
import taffy.core_api.MAX_WORKSPACES
import taffy.core_api.MAX_WORKSPACE_FACTS
import taffy.core_api.MAX_WORKSPACE_SOURCES
import taffy.core_api.MemoryAvailability
import taffy.core_api.SavedDataAvailability
import taffy.core_api.TaskPhase

/** Builds the one deterministic, bounded whole-profile document accepted by Privacy Center. */
class ProfileDataExportEncoder {
    data class Input(
        val core: CoreStatus,
        val preferences: UserPreferences,
        val history: HistorySnapshot,
        val bookmarks: BookmarksSnapshot,
        val downloads: List<DownloadRecord>,
        val downloadsComplete: Boolean,
        val siteSettings: SiteSettingsRepository.Snapshot,
        val filtering: FilteringSettings,
        val timeOnSites: TimeOnSitesRepository.Snapshot,
    )

    fun isReady(input: Input): Boolean {
        val core = input.core
        val history = input.history as? HistorySnapshot.Ready ?: return false
        val bookmarks = input.bookmarks as? BookmarksSnapshot.Ready ?: return false
        val sites = input.siteSettings as? SiteSettingsRepository.Snapshot.Ready ?: return false
        return core.hasCompleteProjection() &&
            core.active_tasks.none { it.phase.isLive() } &&
            core.provider_roster.none { it.signing_in } &&
            core.workspaces.size <= MAX_WORKSPACES &&
            core.workspaces.all {
                it.sources.size <= MAX_WORKSPACE_SOURCES &&
                    it.facts.size <= MAX_WORKSPACE_FACTS &&
                    it.facts.all { fact -> fact.sources.size <= MAX_FACT_SOURCES }
            } &&
            core.provider_roster.size <= MAX_PROVIDER_ROSTER_ENTRIES &&
            core.library.availability == LibraryAvailability.AVAILABLE &&
            core.library.entries.size <= MAX_LIBRARY_ENTRIES &&
            core.library.entries.all { it.sources.size <= MAX_LIBRARY_SOURCES } &&
            core.memory.availability == MemoryAvailability.AVAILABLE &&
            core.memory.records.size <= MAX_MEMORY_RECORDS &&
            core.saved_sign_ins.availability == SavedDataAvailability.READY &&
            core.saved_sign_ins.records.size <= MAX_SAVED_SIGN_INS &&
            core.saved_details.availability == SavedDataAvailability.READY &&
            core.saved_details.people.size <= MAX_SAVED_DETAILS &&
            core.site_skills.size <= MAX_SITE_SKILLS &&
            input.preferences.loaded &&
            history.complete && history.visits.size <= MAX_HISTORY_ROWS &&
            bookmarks.complete && bookmarks.folders.sumOf { it.bookmarks.size } <= MAX_BOOKMARK_ROWS &&
            input.downloadsComplete && input.downloads.size <= MAX_DOWNLOAD_ROWS &&
            sites.sites.size <= MAX_SITE_ROWS &&
            input.filtering.exceptionHosts.size <= MAX_SITE_ROWS &&
            input.timeOnSites.availability == YouSurfaceAvailability.READY &&
            input.timeOnSites.today.size <= MAX_SITE_ROWS &&
            input.timeOnSites.week.size <= MAX_SITE_ROWS
    }

    /** Returns null without invoking a destination when any input is partial or too large. */
    fun encode(input: Input): ByteArray? {
        if (!isReady(input)) return null
        return try {
            BoundedJsonWriter(MAX_CHARACTERS, MAX_BYTES).apply {
                objectValue {
                    field("format", "taffygo-profile-data")
                    field("version", 1)
                    writeBoundaries()
                    writeAccount(input.core)
                    writeAssistant(input.core)
                    writeProviders(input.core)
                    writeWorkspaces(input.core)
                    writeLibrary(input.core)
                    writeMemory(input.core)
                    writeSavedData(input.core)
                    writeSiteSkills(input.core)
                    writeBrowserExportSections(input)
                }
            }.bytes()
        } catch (_: BoundedJsonWriter.SizeLimitExceeded) {
            null
        }
    }

    private fun BoundedJsonWriter.writeBoundaries() {
        fieldArray(
            "notIncluded",
            listOf(
                "passwords, provider keys, account tokens, and other secret material",
                "cookies, cached pages, open tabs, and live task authority",
            ),
        ) { string -> objectValue { field("description", string) } }
        fieldArray(
            "outsideDeletionControl",
            listOf(
                "files already downloaded or exported to a destination you own",
                "copies saved by another app, drive, message, or recipient",
                "data held by websites, AI providers, or the TaffyGo account service",
            ),
        ) { string -> objectValue { field("description", string) } }
    }

    private fun BoundedJsonWriter.writeAccount(core: CoreStatus) {
        fieldObject("account") {
            val account = core.auth_state?.account
            field("signedIn", account != null)
            if (account != null) {
                field("accountId", account.account_id)
                field("displayName", account.display_name)
                field("email", account.email)
                field("method", account.method.name)
            }
        }
    }

    private fun BoundedJsonWriter.writeAssistant(core: CoreStatus) {
        val configuration = core.assistant_configuration
        fieldObject("assistant") {
            field("revision", configuration.revision)
            field("preset", configuration.preset.name)
            field("pace", configuration.pace)
            field("length", configuration.length)
            field("checkIn", configuration.check_in)
            fieldArray("disabledAbilities", configuration.disabled_abilities.sortedBy { it.name }) {
                objectValue { field("ability", it.name) }
            }
        }
    }

    private fun BoundedJsonWriter.writeProviders(core: CoreStatus) {
        fieldArray("providers", core.provider_roster.sortedBy { it.provider_id }) { provider ->
            objectValue {
                field("id", provider.provider_id)
                field("name", provider.display_name)
                field("origin", provider.origin.name)
                field("enabled", provider.enabled)
                field("configured", provider.stored != null)
                field("credentialState", provider.stored?.state?.name)
                field("accountLabel", provider.stored?.account_label)
                field("planLabel", provider.stored?.plan_label)
                field("subscriptionBacked", provider.stored?.subscription_backed ?: false)
                field("endpointHost", provider.endpoint_host)
                field("selectedModel", provider.selected_model_id)
                field("thinking", provider.thinking?.level?.name)
                fieldArray("authMethods", provider.auth_methods.sortedBy { it.name }) {
                    objectValue { field("method", it.name) }
                }
            }
        }
    }

    private fun BoundedJsonWriter.writeWorkspaces(core: CoreStatus) {
        fieldArray("workspaces", core.workspaces.sortedBy { it.workspace_id }) { workspace ->
            objectValue {
                field("id", workspace.workspace_id)
                field("revision", workspace.revision)
                field("name", workspace.display_name)
                field("goal", workspace.goal)
                field("phase", workspace.phase.name)
                field("template", workspace.template_id.name)
                field("saved", workspace.saved)
                field("updatedAtEpochMillis", workspace.last_updated_epoch_ms)
                fieldArray("sources", workspace.sources.sortedBy { it.source_id }) { source ->
                    objectValue {
                        field("id", source.source_id)
                        field("title", source.title)
                        field("host", source.host)
                        field("readAtEpochMillis", source.read_at_epoch_ms)
                        field("excluded", source.excluded)
                    }
                }
                fieldArray("facts", workspace.facts.sortedBy { it.fact_id }) { fact ->
                    objectValue {
                        field("id", fact.fact_id)
                        field("field", fact.field)
                        field("value", fact.value)
                        field("kind", fact.kind.name)
                        field("correction", fact.correction)
                        field("hasConflict", fact.has_conflict)
                        field("needsNewSource", fact.needs_new_source)
                        fieldArray("sourceIds", fact.sources.sorted()) {
                            objectValue { field("id", it) }
                        }
                    }
                }
            }
        }
    }

    private fun BoundedJsonWriter.writeLibrary(core: CoreStatus) {
        fieldArray("library", core.library.entries.sortedBy { it.entry_id }) { entry ->
            objectValue {
                field("id", entry.entry_id)
                field("revision", entry.revision)
                field("collection", entry.collection_name)
                field("field", entry.field)
                field("originalValue", entry.original_value)
                field("correction", entry.correction)
                field("kind", entry.kind.name)
                field("capturedAtEpochMillis", entry.captured_at_epoch_ms)
                field("lastCheckedAtEpochMillis", entry.last_checked_epoch_ms)
                field("hasConflict", entry.has_conflict)
                fieldArray("sources", entry.sources.sortedBy { it.source_id }) { source ->
                    objectValue {
                        field("title", source.title)
                        field("host", source.host)
                        field("observedAtEpochMillis", source.observed_at_epoch_ms)
                    }
                }
            }
        }
    }

    private fun BoundedJsonWriter.writeMemory(core: CoreStatus) {
        fieldArray("memory", core.memory.records.sortedBy { it.memory_id }) { memory ->
            objectValue {
                field("id", memory.memory_id)
                field("revision", memory.revision)
                field("statement", memory.statement)
                field("source", memory.source_kind.name)
                field("scope", memory.scope_kind.name)
                field("scopeWorkspace", memory.scope_workspace?.display_name)
                field("sensitivity", memory.sensitivity.name)
                field("createdAtEpochMillis", memory.created_at_epoch_ms)
                field("updatedAtEpochMillis", memory.updated_at_epoch_ms)
                field("reviewedAtEpochMillis", memory.reviewed_at_epoch_ms)
                field("expiresAtEpochMillis", memory.expires_at_epoch_ms)
            }
        }
    }

    private fun BoundedJsonWriter.writeSavedData(core: CoreStatus) {
        fieldArray("savedSignIns", core.saved_sign_ins.records.sortedWith(
            compareBy({ it.site }, { it.username }, { it.id }),
        )) { signIn ->
            objectValue {
                field("site", signIn.site)
                field("username", signIn.username)
                field("lastUsedAtEpochMillis", signIn.last_used_epoch_ms)
            }
        }
        fieldArray("savedDetails", core.saved_details.people.sortedBy { it.id }) { person ->
            objectValue {
                field("givenName", person.given_name)
                field("familyName", person.family_name)
                field("email", person.email)
                field("phone", person.phone)
                field("address", person.address)
                field("postcode", person.postcode)
                field("country", person.country)
            }
        }
    }

    private fun BoundedJsonWriter.writeSiteSkills(core: CoreStatus) {
        fieldArray("savedSiteSkills", core.site_skills.sortedBy { it.skill_id }) { skill ->
            objectValue {
                field("id", skill.skill_id)
                field("origin", skill.origin)
                field("provenance", skill.provenance.name)
                field("status", skill.status.name)
                field("version", skill.active_version)
                field("stepCount", skill.step_count)
                field("installedAtEpochMillis", skill.installed_at_epoch_ms)
                field("updatedAtEpochMillis", skill.updated_at_epoch_ms)
            }
        }
    }

    private fun TaskPhase.isLive(): Boolean = when (this) {
        TaskPhase.COMPLETED,
        TaskPhase.PARTIAL,
        TaskPhase.FAILED,
        TaskPhase.CANCELLED,
        TaskPhase.OUTCOME_UNKNOWN,
        -> false
        else -> true
    }

    private companion object {
        const val MAX_BYTES = 8 * 1024 * 1024
        const val MAX_CHARACTERS = 4 * 1024 * 1024
        const val MAX_HISTORY_ROWS = 1_024
        const val MAX_BOOKMARK_ROWS = 1_024
        const val MAX_DOWNLOAD_ROWS = 256
        const val MAX_SITE_ROWS = 4_096
    }
}

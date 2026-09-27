// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.content.Context
import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.feature.settings.SiteSettingsRepository
import java.io.Closeable
import kotlin.coroutines.resume
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.profiles.Profile

/** Publishes and mutates only settings read from the original regular profile. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class ChromiumSiteSettingsRepository constructor(
    private val backend: Backend,
    private val assertUiThread: () -> Unit,
) : SiteSettingsRepository,
    Closeable {
    constructor(context: Context, profile: Profile) : this(
        ChromiumSiteSettingsBackend(context, requireRegularProfile(profile)),
        { ThreadUtils.assertOnUiThread() },
    )

    private val mutableSnapshot = MutableStateFlow<SiteSettingsRepository.Snapshot>(
        SiteSettingsRepository.Snapshot.Loading,
    )
    override val snapshot: StateFlow<SiteSettingsRepository.Snapshot> =
        mutableSnapshot.asStateFlow()

    private var generation = 0L
    private var writing = false
    private var closed = false
    private var pendingSiteReadBack: CancellableContinuation<List<SiteFact>?>? = null

    init {
        refresh()
    }

    override fun refresh() {
        assertUiThread()
        if (closed || writing) return
        val request = ++generation
        mutableSnapshot.value = SiteSettingsRepository.Snapshot.Loading
        val defaults = try {
            SiteSettingsRepository.Capability.entries.mapNotNull { capability ->
                backend.readDefault(capability)?.let { value ->
                    SiteSettingsRepository.DefaultSetting(
                        capability = capability,
                        enabled = value.enabled,
                        userModifiable = value.userModifiable,
                    )
                }
            }
        } catch (_: RuntimeException) {
            publishUnavailable(request)
            return
        }
        try {
            backend.fetchSites { sites ->
                if (closed || request != generation) return@fetchSites
                mutableSnapshot.value = if (sites == null) {
                    SiteSettingsRepository.Snapshot.Unavailable
                } else {
                    SiteSettingsRepository.Snapshot.Ready(
                        defaults = defaults,
                        sites = mergeSites(sites),
                    )
                }
            }
        } catch (_: RuntimeException) {
            publishUnavailable(request)
        }
    }

    override suspend fun setDefault(
        capability: SiteSettingsRepository.Capability,
        enabled: Boolean,
    ): SiteSettingsRepository.UpdateResult {
        assertUiThread()
        if (closed) return SiteSettingsRepository.UpdateResult.UNAVAILABLE
        if (writing) return SiteSettingsRepository.UpdateResult.REFUSED
        val ready = mutableSnapshot.value as? SiteSettingsRepository.Snapshot.Ready
            ?: return SiteSettingsRepository.UpdateResult.UNAVAILABLE
        val current = ready.defaults.firstOrNull { it.capability == capability }
            ?: return SiteSettingsRepository.UpdateResult.UNSUPPORTED
        if (!current.userModifiable) return SiteSettingsRepository.UpdateResult.REFUSED

        writing = true
        return try {
            backend.writeDefault(capability, enabled)
            val verified = backend.readDefault(capability)
            if (verified != null) publishReadBack(ready, capability, verified)
            if (verified?.enabled == enabled) {
                SiteSettingsRepository.UpdateResult.APPLIED
            } else {
                SiteSettingsRepository.UpdateResult.FAILED
            }
        } catch (_: RuntimeException) {
            SiteSettingsRepository.UpdateResult.FAILED
        } finally {
            writing = false
        }
    }

    override suspend fun resetSite(host: String): SiteSettingsRepository.UpdateResult {
        assertUiThread()
        if (closed) return SiteSettingsRepository.UpdateResult.UNAVAILABLE
        if (writing) return SiteSettingsRepository.UpdateResult.REFUSED
        val ready = mutableSnapshot.value as? SiteSettingsRepository.Snapshot.Ready
            ?: return SiteSettingsRepository.UpdateResult.UNAVAILABLE
        if (ready.sites.none { it.host == host }) {
            return SiteSettingsRepository.UpdateResult.REFUSED
        }

        writing = true
        val request = ++generation
        return try {
            if (!backend.resetSite(host)) {
                SiteSettingsRepository.UpdateResult.REFUSED
            } else {
                val facts = fetchSitesForReadBack()
                if (closed || request != generation) {
                    SiteSettingsRepository.UpdateResult.UNAVAILABLE
                } else if (facts == null) {
                    mutableSnapshot.value = SiteSettingsRepository.Snapshot.Unavailable
                    SiteSettingsRepository.UpdateResult.FAILED
                } else {
                    val verifiedSites = mergeSites(facts)
                    mutableSnapshot.value = ready.copy(sites = verifiedSites)
                    if (verifiedSites.none { it.host == host }) {
                        SiteSettingsRepository.UpdateResult.APPLIED
                    } else {
                        SiteSettingsRepository.UpdateResult.FAILED
                    }
                }
            }
        } catch (_: RuntimeException) {
            SiteSettingsRepository.UpdateResult.FAILED
        } finally {
            writing = false
        }
    }

    private suspend fun fetchSitesForReadBack(): List<SiteFact>? =
        suspendCancellableCoroutine { continuation ->
            if (pendingSiteReadBack != null) {
                continuation.resume(null)
                return@suspendCancellableCoroutine
            }
            pendingSiteReadBack = continuation
            continuation.invokeOnCancellation {
                if (pendingSiteReadBack === continuation) pendingSiteReadBack = null
            }
            try {
                backend.fetchSites { sites ->
                    if (pendingSiteReadBack === continuation) pendingSiteReadBack = null
                    if (continuation.isActive) continuation.resume(sites)
                }
            } catch (error: RuntimeException) {
                if (pendingSiteReadBack === continuation) pendingSiteReadBack = null
                if (continuation.isActive) continuation.resume(null)
            }
        }

    private fun publishReadBack(
        ready: SiteSettingsRepository.Snapshot.Ready,
        capability: SiteSettingsRepository.Capability,
        verified: DefaultValue,
    ) {
        if (closed) return
        mutableSnapshot.value = ready.copy(
            defaults = ready.defaults.map { setting ->
                if (setting.capability == capability) {
                    setting.copy(
                        enabled = verified.enabled,
                        userModifiable = verified.userModifiable,
                    )
                } else {
                    setting
                }
            },
        )
    }

    private fun publishUnavailable(request: Long) {
        if (!closed && request == generation) {
            mutableSnapshot.value = SiteSettingsRepository.Snapshot.Unavailable
        }
    }

    override fun close() {
        assertUiThread()
        if (closed) return
        closed = true
        generation++
        writing = false
        pendingSiteReadBack?.let { continuation ->
            if (continuation.isActive) continuation.resume(null)
        }
        pendingSiteReadBack = null
        try {
            backend.close()
        } finally {
            mutableSnapshot.value = SiteSettingsRepository.Snapshot.Unavailable
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    interface Backend : Closeable {
        fun readDefault(capability: SiteSettingsRepository.Capability): DefaultValue?
        fun writeDefault(capability: SiteSettingsRepository.Capability, enabled: Boolean)
        fun fetchSites(callback: (List<SiteFact>?) -> Unit)
        fun resetSite(host: String): Boolean
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    data class DefaultValue(val enabled: Boolean, val userModifiable: Boolean)

    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    data class SiteFact(
        val host: String,
        val changedPermissionCount: Int,
        val changedCapabilities: Set<SiteSettingsRepository.Capability>,
    )

    private companion object {
        fun requireRegularProfile(profile: Profile): Profile {
            require(!profile.isOffTheRecord) {
                "Site settings must be rooted in the original regular profile"
            }
            return profile
        }

        fun mergeSites(facts: List<SiteFact>): List<SiteSettingsRepository.Entry> {
            val merged = HashMap<String, MutableSite>(
                facts.size.coerceAtMost(MAX_INITIAL_SITE_CAPACITY),
            )
            for (fact in facts) {
                if (
                    fact.host.isBlank() || fact.host == "*" || fact.changedPermissionCount <= 0
                ) {
                    continue
                }
                val site = merged.getOrPut(fact.host) { MutableSite() }
                site.changedPermissionCount = (
                    site.changedPermissionCount.toLong() + fact.changedPermissionCount
                ).coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
                site.changedCapabilities.addAll(fact.changedCapabilities)
            }
            return merged.map { (host, site) ->
                SiteSettingsRepository.Entry(
                    host = host,
                    changedPermissionCount = site.changedPermissionCount,
                    changedCapabilities = site.changedCapabilities.toSet(),
                )
            }
                .sortedBy(SiteSettingsRepository.Entry::host)
        }

        private const val MAX_INITIAL_SITE_CAPACITY = 4_096

        private data class MutableSite(
            var changedPermissionCount: Int = 0,
            val changedCapabilities: MutableSet<SiteSettingsRepository.Capability> =
                mutableSetOf(),
        )
    }
}

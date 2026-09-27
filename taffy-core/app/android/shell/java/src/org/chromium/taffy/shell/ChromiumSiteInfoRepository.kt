// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.feature.browsing.SiteInfoRepository
import com.taffygo.browser.ui.feature.settings.SiteSettingsRepository
import java.io.Closeable
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import org.chromium.base.ThreadUtils
import org.chromium.base.lifetime.Destroyable
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tab.TabUtils
import org.chromium.chrome.browser.tabmodel.TabModelSelector

/** Live site facts and commands for the exact selected committed regular tab. */
class ChromiumSiteInfoRepository @VisibleForTesting constructor(
    private val environment: Environment,
    private val dispatchers: AppDispatchers,
    private val permissionStore: PermissionStore,
) : SiteInfoRepository, Destroyable, Closeable {

    /** Browser lookup seam for native-free race and host-binding tests. */
    @VisibleForTesting
    fun interface Environment {
        fun selectedPage(expectedHost: String?): Page?
    }

    /** The selected Tab/WebContents pair after its committed host was checked. */
    @VisibleForTesting
    interface Page {
        val desktopSite: Boolean
        fun setDesktopSite(enabled: Boolean)
    }

    /** Selected-site view over the profile settings catalog. */
    @VisibleForTesting
    interface PermissionStore : Closeable {
        val revision: StateFlow<Long>
        fun permissionsFor(host: String): SiteInfoRepository.PermissionState
        fun refresh()
        suspend fun resetSite(host: String): SiteInfoRepository.PermissionResetResult
    }

    constructor(
        selector: TabModelSelector,
        siteSettings: SiteSettingsRepository,
        dispatchers: AppDispatchers,
        scope: CoroutineScope,
    ) : this(
        ChromiumEnvironment(selector),
        dispatchers,
        ChromiumSiteInfoPermissionAdapter(siteSettings, scope),
    )

    private var closed = false

    override val desktopSiteAvailable: Boolean
        get() = !closed && environment.selectedPage(expectedHost = null) != null

    override val permissionRevision: StateFlow<Long>
        get() = permissionStore.revision

    override fun isDesktopSite(host: String): Boolean {
        if (closed || host.isBlank()) return false
        return environment.selectedPage(host)?.desktopSite ?: false
    }

    override fun permissionsFor(host: String): SiteInfoRepository.PermissionState {
        if (closed || host.isBlank() || environment.selectedPage(host) == null) {
            return SiteInfoRepository.PermissionState.Unavailable
        }
        return permissionStore.permissionsFor(host)
    }

    override suspend fun refreshPermissions(host: String) {
        if (host.isBlank()) return
        withContext(dispatchers.main) {
            if (closed || environment.selectedPage(host) == null) return@withContext
            permissionStore.refresh()
        }
    }

    override suspend fun setDesktopSite(host: String, enabled: Boolean) {
        if (host.isBlank()) return
        withContext(dispatchers.main) {
            if (closed) return@withContext
            // Resolve again at execution time. The sheet may have been open
            // while a different tab or a different host became selected.
            environment.selectedPage(host)?.setDesktopSite(enabled)
        }
    }

    override suspend fun resetPermissions(
        host: String,
    ): SiteInfoRepository.PermissionResetResult {
        if (host.isBlank()) return SiteInfoRepository.PermissionResetResult.REFUSED
        return withContext(dispatchers.main) {
            if (closed) return@withContext SiteInfoRepository.PermissionResetResult.UNAVAILABLE
            // Resolve again at execution time and require a current, visible
            // exception. A private, replaced, or still-loading page cannot
            // spend a profile mutation through a stale sheet.
            if (environment.selectedPage(host) == null) {
                return@withContext SiteInfoRepository.PermissionResetResult.REFUSED
            }
            val permissions = permissionStore.permissionsFor(host)
            if (permissions !is SiteInfoRepository.PermissionState.Changed) {
                return@withContext SiteInfoRepository.PermissionResetResult.REFUSED
            }
            permissionStore.resetSite(host)
        }
    }

    override fun destroy() {
        if (closed) return
        closed = true
        permissionStore.close()
    }

    override fun close() = destroy()

    private class ChromiumEnvironment(
        private val selector: TabModelSelector,
    ) : Environment {
        override fun selectedPage(expectedHost: String?): Page? {
            ThreadUtils.assertOnUiThread()
            val tab = selector.currentTab ?: return null
            if (tab.isOffTheRecord || tab.isDestroyed || tab.isClosing) return null
            val contents = tab.webContents ?: return null
            if (contents.isDestroyed) return null
            val address = contents.lastCommittedUrl
            if (
                !address.isValid ||
                (address.scheme != "http" && address.scheme != "https") ||
                address.host.isBlank() ||
                address.username.isNotEmpty() ||
                address.password.isNotEmpty() ||
                (expectedHost != null && address.host != expectedHost)
            ) {
                return null
            }
            return ChromiumPage(tab)
        }
    }

    private class ChromiumPage(
        private val tab: Tab,
    ) : Page {
        override val desktopSite: Boolean
            get() = TabUtils.isUsingDesktopUserAgent(tab.webContents)

        override fun setDesktopSite(enabled: Boolean) {
            // Re-resolved by ChromiumEnvironment immediately before this call;
            // TabUtils delegates to this WebContents' NavigationController and
            // reloads only when the user-agent choice actually changes.
            TabUtils.switchUserAgent(tab, enabled)
        }
    }
}

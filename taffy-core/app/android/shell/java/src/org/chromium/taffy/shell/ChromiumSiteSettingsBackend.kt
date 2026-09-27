// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.content.Context
import com.taffygo.browser.ui.feature.settings.SiteSettingsRepository
import java.util.Locale
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.site_settings.ChromeSiteSettingsDelegate
import org.chromium.components.browser_ui.site_settings.SiteSettingsCategory
import org.chromium.components.browser_ui.site_settings.SiteDataCleaner
import org.chromium.components.browser_ui.site_settings.Website
import org.chromium.components.browser_ui.site_settings.WebsitePermissionsFetcher
import org.chromium.components.browser_ui.site_settings.WebsitePreferenceBridge

/** The Chromium API vocabulary hidden behind [ChromiumSiteSettingsRepository.Backend]. */
internal class ChromiumSiteSettingsBackend(
    context: Context,
    private val profile: Profile,
) : ChromiumSiteSettingsRepository.Backend {
    private val delegate = ChromeSiteSettingsDelegate(context, profile)
    private val fetcher = WebsitePermissionsFetcher(delegate)
    private val contentTypes = SiteSettingsRepository.Capability.entries.associateWith {
        SiteSettingsCategory.contentSettingsType(categoryType(it))
    }
    private val capabilitiesByContentType = contentTypes.entries.associate { (capability, type) ->
        type to capability
    }
    private var closed = false
    private var fetchGeneration = 0L
    private var sitesByHost: Map<String, List<Website>> = emptyMap()

    override fun readDefault(
        capability: SiteSettingsRepository.Capability,
    ): ChromiumSiteSettingsRepository.DefaultValue? {
        if (closed) return null
        val contentType = contentTypes.getValue(capability)
        if (WebsitePreferenceBridge.requiresTriStateContentSetting(contentType)) return null
        return ChromiumSiteSettingsRepository.DefaultValue(
            enabled = WebsitePreferenceBridge.isCategoryEnabled(profile, contentType),
            userModifiable = WebsitePreferenceBridge.isContentSettingUserModifiable(
                profile,
                contentType,
            ),
        )
    }

    override fun writeDefault(
        capability: SiteSettingsRepository.Capability,
        enabled: Boolean,
    ) {
        val current = readDefault(capability)
            ?: throw UnsupportedOperationException("Unsupported site-setting capability")
        if (!current.userModifiable) throw SecurityException("The site-setting default is managed")
        WebsitePreferenceBridge.setCategoryEnabled(
            profile,
            contentTypes.getValue(capability),
            enabled,
        )
    }

    override fun fetchSites(
        callback: (List<ChromiumSiteSettingsRepository.SiteFact>?) -> Unit,
    ) {
        if (closed) {
            callback(null)
            return
        }
        val request = ++fetchGeneration
        fetcher.fetchAllPreferences { websites ->
            if (closed || request != fetchGeneration) return@fetchAllPreferences
            val projected = ArrayList<ChromiumSiteSettingsRepository.SiteFact>()
            val grouped = HashMap<String, MutableList<Website>>()
            for (website in websites) {
                val fact = projectSite(website) ?: continue
                projected += fact
                grouped.getOrPut(fact.host) { mutableListOf() }.add(website)
            }
            sitesByHost = grouped
            callback(projected)
        }
    }

    override fun resetSite(host: String): Boolean {
        if (closed) return false
        val websites = sitesByHost[host] ?: return false
        for (website in websites) SiteDataCleaner.resetPermissions(profile, website)
        return true
    }

    private fun projectSite(website: Website): ChromiumSiteSettingsRepository.SiteFact? {
        val host = website.mainAddress.host
            ?.trim()
            ?.lowercase(Locale.ROOT)
            ?.takeIf { it.isNotEmpty() && it != "*" }
            ?: return null
        val changedCount = website.contentSettingExceptions.size +
            website.permissionInfos.size +
            website.embeddedPermissions.values.sumOf { it.size } +
            website.chosenObjectInfo.size
        if (changedCount == 0) return null
        val capabilities = buildSet {
            website.contentSettingExceptions.mapTo(this) { it.contentSettingType }
            website.permissionInfos.mapTo(this) { it.contentSettingsType }
            addAll(website.embeddedPermissions.keys)
        }.mapNotNullTo(mutableSetOf()) { capabilitiesByContentType[it] }
        return ChromiumSiteSettingsRepository.SiteFact(
            host = host,
            changedPermissionCount = changedCount,
            changedCapabilities = capabilities,
        )
    }

    override fun close() {
        if (closed) return
        closed = true
        fetchGeneration++
        sitesByHost = emptyMap()
        delegate.onDestroyView()
    }

    private companion object {
        fun categoryType(capability: SiteSettingsRepository.Capability): Int = when (capability) {
            SiteSettingsRepository.Capability.LOCATION -> SiteSettingsCategory.Type.DEVICE_LOCATION
            SiteSettingsRepository.Capability.CAMERA -> SiteSettingsCategory.Type.CAMERA
            SiteSettingsRepository.Capability.MICROPHONE -> SiteSettingsCategory.Type.MICROPHONE
            SiteSettingsRepository.Capability.NOTIFICATIONS -> SiteSettingsCategory.Type.NOTIFICATIONS
            SiteSettingsRepository.Capability.JAVASCRIPT -> SiteSettingsCategory.Type.JAVASCRIPT
            SiteSettingsRepository.Capability.POP_UPS -> SiteSettingsCategory.Type.POPUPS
            SiteSettingsRepository.Capability.AUTOMATIC_DOWNLOADS ->
                SiteSettingsCategory.Type.AUTOMATIC_DOWNLOADS
            SiteSettingsRepository.Capability.CLIPBOARD -> SiteSettingsCategory.Type.CLIPBOARD
            SiteSettingsRepository.Capability.SENSORS -> SiteSettingsCategory.Type.SENSORS
            SiteSettingsRepository.Capability.SOUND -> SiteSettingsCategory.Type.SOUND
        }
    }
}

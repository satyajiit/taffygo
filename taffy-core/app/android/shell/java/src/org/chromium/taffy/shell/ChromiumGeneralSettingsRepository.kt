// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.feature.settings.GeneralSettingsRepository
import java.io.Closeable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.download.DirectoryOption
import org.chromium.chrome.browser.download.DownloadDialogBridge
import org.chromium.chrome.browser.download.DownloadDirectoryProvider
import org.chromium.chrome.browser.profiles.Profile

/**
 * The regular profile's live Chromium download-directory preference.
 *
 * Filesystem paths remain in this adapter. The settings surface receives only
 * opaque identities and display-safe kinds, and a write succeeds only after
 * Chromium reads the exact value back from the same original profile.
 */
internal class ChromiumGeneralSettingsRepository(
    private val profile: Profile,
    private val directories: DownloadDirectoryProvider = DownloadDirectoryProvider.getInstance(),
    private val locationStore: ChromiumDownloadLocationStore = BrowserDownloadLocationStore,
) : GeneralSettingsRepository,
    Closeable {
    private val mutableSnapshot = MutableStateFlow(
        GeneralSettingsRepository.Snapshot(downloadLocationsLoading = true),
    )
    override val snapshot: StateFlow<GeneralSettingsRepository.Snapshot> =
        mutableSnapshot.asStateFlow()

    private var locationsById: Map<String, String> = emptyMap()
    private val idsByPath = mutableMapOf<String, String>()
    private var nextLocationId = 1L
    private var refreshGeneration = 0L
    private var closed = false

    init {
        require(!profile.isOffTheRecord) {
            "Download settings must be rooted in the original regular profile"
        }
        refreshDownloadLocations()
    }

    override fun refreshDownloadLocations() {
        ThreadUtils.assertOnUiThread()
        if (closed) return
        val generation = ++refreshGeneration
        mutableSnapshot.value = mutableSnapshot.value.copy(
            downloadLocationsLoading = true,
            downloadLocationSelectionFailed = false,
        )
        try {
            directories.getAllDirectoriesOptions { options ->
                if (closed || generation != refreshGeneration) return@getAllDirectoriesOptions
                publish(options)
            }
        } catch (_: RuntimeException) {
            locationsById = emptyMap()
            mutableSnapshot.value = mutableSnapshot.value.copy(
                downloadLocationsLoading = false,
                downloadLocations = emptyList(),
                selectedDownloadLocationId = null,
                downloadLocationSelectionFailed = true,
            )
        }
    }

    override suspend fun chooseDownloadLocation(
        id: String,
    ): GeneralSettingsRepository.DownloadLocationChoice {
        ThreadUtils.assertOnUiThread()
        if (closed) return GeneralSettingsRepository.DownloadLocationChoice.SELECTION_UNAVAILABLE
        val path = locationsById[id]
            ?: return GeneralSettingsRepository.DownloadLocationChoice.SELECTION_UNAVAILABLE
        val result = persistAndVerifyDownloadLocation(profile, path, locationStore)
        if (result != GeneralSettingsRepository.DownloadLocationChoice.SAVED) return result
        mutableSnapshot.value = mutableSnapshot.value.copy(
            selectedDownloadLocationId = id,
            downloadLocationReadFailed = false,
        )
        return GeneralSettingsRepository.DownloadLocationChoice.SAVED
    }

    private fun publish(options: List<DirectoryOption>) {
        val mapped = mutableListOf<GeneralSettingsRepository.DownloadLocation>()
        val paths = linkedMapOf<String, String>()
        val seenPaths = hashSetOf<String>()
        options.forEach { option ->
            val path = option.location?.takeIf(String::isNotBlank) ?: return@forEach
            val kind = when (option.type) {
                DirectoryOption.DownloadLocationDirectoryType.DEFAULT ->
                    GeneralSettingsRepository.DownloadLocation.Kind.DEVICE
                DirectoryOption.DownloadLocationDirectoryType.ADDITIONAL ->
                    GeneralSettingsRepository.DownloadLocation.Kind.REMOVABLE_STORAGE
                else -> return@forEach
            }
            if (!seenPaths.add(path)) return@forEach
            val id = idsByPath.getOrPut(path) { "location-${nextLocationId++}" }
            paths[id] = path
            mapped += GeneralSettingsRepository.DownloadLocation(id, kind)
        }
        // Removed volumes must not leave one path and identifier resident for
        // the rest of the profile lifetime. The set also makes duplicate
        // detection linear instead of repeatedly scanning `paths.values`.
        idsByPath.keys.retainAll(seenPaths)
        locationsById = paths
        if (mapped.isEmpty()) {
            mutableSnapshot.value = GeneralSettingsRepository.Snapshot(
                searchEngineAvailable = true,
                downloadLocationsLoading = false,
                downloadLocationSelectionFailed = true,
            )
            return
        }
        var readFailed = false
        val selectedPath = try {
            locationStore.read(profile)
        } catch (_: RuntimeException) {
            readFailed = true
            ""
        }
        mutableSnapshot.value = GeneralSettingsRepository.Snapshot(
            searchEngineAvailable = true,
            downloadLocationsLoading = false,
            downloadLocations = mapped,
            selectedDownloadLocationId = paths.entries.firstOrNull {
                it.value == selectedPath
            }?.key,
            downloadLocationSelectionFailed = false,
            downloadLocationReadFailed = readFailed,
        )
    }

    override fun close() {
        ThreadUtils.assertOnUiThread()
        if (closed) return
        closed = true
        refreshGeneration++
        locationsById = emptyMap()
        idsByPath.clear()
        mutableSnapshot.value = GeneralSettingsRepository.Snapshot(
            searchEngineAvailable = false,
            downloadLocationsLoading = false,
        )
    }
}

private object BrowserDownloadLocationStore : ChromiumDownloadLocationStore {
    override fun write(profile: Profile, path: String) {
        DownloadDialogBridge.setDownloadAndSaveFileDefaultDirectory(profile, path)
    }

    override fun read(profile: Profile): String =
        DownloadDialogBridge.getDownloadDefaultDirectory(profile)
}

@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun persistAndVerifyDownloadLocation(
    profile: Profile,
    path: String,
    store: ChromiumDownloadLocationStore,
): GeneralSettingsRepository.DownloadLocationChoice {
    try {
        store.write(profile, path)
    } catch (_: RuntimeException) {
        return GeneralSettingsRepository.DownloadLocationChoice.WRITE_FAILED
    }
    val stored = try {
        store.read(profile)
    } catch (_: RuntimeException) {
        return GeneralSettingsRepository.DownloadLocationChoice.READBACK_FAILED
    }
    return if (stored == path) {
        GeneralSettingsRepository.DownloadLocationChoice.SAVED
    } else {
        GeneralSettingsRepository.DownloadLocationChoice.READBACK_FAILED
    }
}

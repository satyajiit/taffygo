// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.AndroidProfileSecureMaterialStore
import com.taffygo.browser.ui.app.ProfilePreferenceStore
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import java.security.MessageDigest
import java.util.Base64
import kotlinx.coroutines.CoroutineDispatcher
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.profiles.ProfileResolver

/** Real Android/browser ports that are owned and closed with one Chromium profile. */
class ChromiumProfilePorts(profile: Profile, cryptographyDispatcher: CoroutineDispatcher) {
    init {
        check(!profile.isOffTheRecord) {
            "Persistent Android profile ports cannot be created for an off-the-record profile"
        }
    }

    val preferenceStore: ProfilePreferenceStore = ChromiumProfilePreferenceStore(profile)
    val secureMaterial: AndroidProfileSecureMaterialStore = AndroidProfileSecureMaterialStore.create(
        preferences = preferenceStore,
        profileStorageNamespace = secureStorageNamespace(profile),
        cryptographyDispatcher = cryptographyDispatcher,
    )

    fun ownWith(lifetime: TaffyProfileLifetime) {
        lifetime.own(secureMaterial)
    }

    /** Revokes this profile's Keystore namespace before Android clears application data. */
    suspend fun destroySecureMaterial() = secureMaterial.destroyAll()

    private companion object {
        fun secureStorageNamespace(profile: Profile): String {
            val profileToken = ProfileResolver().tokenize(profile)
            require(profileToken.isNotEmpty()) { "Profile secure-storage token is empty" }
            val partitionedToken = buildString {
                append(profileToken)
                append(":regular")
            }
            val tokenBytes = partitionedToken.encodeToByteArray()
            val digest = try {
                MessageDigest.getInstance("SHA-256").digest(tokenBytes)
            } finally {
                tokenBytes.fill(0)
            }
            return Base64.getUrlEncoder().withoutPadding().encodeToString(digest)
                .also { digest.fill(0) }
        }
    }
}

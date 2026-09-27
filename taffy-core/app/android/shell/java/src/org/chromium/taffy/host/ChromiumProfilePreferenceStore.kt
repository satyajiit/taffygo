// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.ProfilePreferenceStore
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.components.prefs.PrefService
import org.chromium.components.user_prefs.UserPrefs

/** Exact Android projection of the PrefService owned by one Chromium profile. */
class ChromiumProfilePreferenceStore(profile: Profile) : ProfilePreferenceStore {
    private val preferences: PrefService

    init {
        check(UserPrefs.areNativePrefsLoaded(profile)) {
            "Taffy profile ports require the profile PrefService to be loaded"
        }
        preferences = UserPrefs.get(profile)
    }

    override fun getString(name: String): String = preferences.getString(name)

    override fun putString(name: String, value: String) {
        preferences.setString(name, value)
    }

    override fun getBoolean(name: String): Boolean = preferences.getBoolean(name)

    override fun putBoolean(name: String, value: Boolean) {
        preferences.setBoolean(name, value)
    }
}

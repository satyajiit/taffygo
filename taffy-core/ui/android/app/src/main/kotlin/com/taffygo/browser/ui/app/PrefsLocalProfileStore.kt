// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.content.Context
import androidx.core.content.edit

/**
 * The profile's bytes, in the private preference file it has always used.
 *
 * `taffy_you_profile` predates the local profile — it held one key,
 * `banner_id` — and keeping the file name is what makes a person's existing
 * picture survive the change (see
 * [ProfileLocalProfileRepository.migrateAndRead]). A new file would have
 * been a silent reset of every profile on every installed build.
 *
 * `MODE_PRIVATE`, so this is readable by this application and nothing else.
 */
class PrefsLocalProfileStore(context: Context) : LocalProfileStore {
    private val prefs = context.applicationContext.getSharedPreferences(
        ProfileLocalProfileRepository.PREFS_NAME,
        Context.MODE_PRIVATE,
    )

    override fun getString(key: String): String? = prefs.getString(key, null)

    /** A null value removes the key, so absent and cleared stay the same state. */
    override fun putString(key: String, value: String?) {
        prefs.edit {
            if (value == null) remove(key) else putString(key, value)
        }
    }

    override fun remove(key: String) {
        prefs.edit { remove(key) }
    }
}

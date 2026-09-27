// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Narrow profile-owned preference port implemented by Chromium's PrefService. */
interface ProfilePreferenceStore {
    fun getString(name: String): String
    fun putString(name: String, value: String)
    fun getBoolean(name: String): Boolean
    fun putBoolean(name: String, value: Boolean)
}

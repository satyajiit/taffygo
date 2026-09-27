// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/**
 * The bytes behind this phone's profile.
 *
 * A seam rather than a direct `SharedPreferences` call so
 * [ProfileLocalProfileRepository] — including its one-shot migration, which
 * is the part that can go wrong — is a host test away instead of a device
 * test away.
 *
 * Nullable values throughout: absent and empty are different states here.
 * A name that was never given and a name that was cleared are the same
 * profile, but a *key* that is absent and a key holding `""` are not the
 * same storage, and the migration has to tell them apart.
 */
interface LocalProfileStore {
    fun getString(key: String): String?
    fun putString(key: String, value: String?)
    fun remove(key: String)
}

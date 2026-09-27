// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.preferences

import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.model.LocalProfile
import kotlinx.coroutines.flow.StateFlow

/**
 * The one door to this phone's profile.
 *
 * Deliberately separate from [UserPreferencesRepository], which projects
 * browser-owned preference state across the Core API. This is neither
 * browser state nor a preference: it is a name and a picture that exist only
 * on this device, that no other process reads, and that survive nothing but
 * this installation. Putting them behind the same port would make a
 * device-local fact look like something the browser holds.
 *
 * The value is a `StateFlow` with its stored value already in it, so a first
 * frame draws the person's own face rather than a default that is replaced a
 * moment later.
 */
interface LocalProfileRepository {

    /** The profile as it is now. */
    val profile: StateFlow<LocalProfile>

    /**
     * Set or clear the name.
     *
     * Normalization is the implementation's job, through
     * [LocalProfile.normalizedDisplayName], so a caller cannot store a name
     * the rest of the product would have rejected.
     */
    suspend fun setDisplayName(name: String?)

    /** Choose the face. */
    suspend fun setAvatar(avatar: LocalAvatar)
}

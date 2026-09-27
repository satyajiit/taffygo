// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.preferences

import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.model.LocalProfile
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * A profile that remembers for as long as the object lives.
 *
 * The normalization rules are the real ones — this is not a stub that accepts
 * anything — so a screen driven by it in a preview or a host test behaves the
 * way it will on a device, and a test that stores an over-long name sees it
 * bounded here too.
 */
class InMemoryLocalProfileRepository(
    initial: LocalProfile = LocalProfile(),
) : LocalProfileRepository {
    private val current = MutableStateFlow(
        initial.copy(displayName = LocalProfile.normalizedDisplayName(initial.displayName)),
    )

    override val profile: StateFlow<LocalProfile> = current.asStateFlow()

    override suspend fun setDisplayName(name: String?) {
        current.value = current.value.copy(
            displayName = LocalProfile.normalizedDisplayName(name),
        )
    }

    override suspend fun setAvatar(avatar: LocalAvatar) {
        current.value = current.value.copy(avatar = avatar)
    }
}

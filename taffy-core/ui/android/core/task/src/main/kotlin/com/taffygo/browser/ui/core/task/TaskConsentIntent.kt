// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.TaskAttachedStore

/** The exact consent a composer showed, submitted as UI intent; it grants no authority. */
data class TaskConsentIntent(
    val sourceHosts: List<String>,
    val sourceDiscoveryEnabled: Boolean,
    val newSourceCap: Int,
    val providerRoute: ProviderRoute,
    /**
     * The stores the person attached whole, which reach Taffy as tools it may
     * search and list (decision 0133). Empty is the ordinary request.
     */
    val attachedStores: Set<TaskAttachedStore> = emptySet(),
)

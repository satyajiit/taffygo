// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

/**
 * One Memory suggestion after a finished task.
 *
 * Never from passwords, codes, cards, health, ids, private tabs, or a page
 * that was not in the task. Absent when nothing qualifies.
 */
data class RememberThisSuggestion(
    val statement: String,
    val why: String,
)

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

/**
 * Browser-owned service availability and the Android task projections.
 *
 * [task] is the one task the surfaces follow — the one a composer started, or
 * failing that the one under way — and [tasks] is every task the core is
 * holding. The list exists because the core can hold more than one session
 * (a restored task beside a fresh start), and a repository that projected
 * only `active_tasks.first()` made a task that had just been admitted
 * invisible for as long as any other session sat ahead of it. A composer
 * waiting for its start to appear reads the list; a bar or a card reads the
 * followed one.
 *
 * The list is derived rather than stored so that it can never disagree with
 * the followed task: `copy(task = …)` on a stored list would have kept the old
 * list beside the new task, which is exactly the shape every test double's
 * `publish` takes. [others] is what the core holds *besides* the followed
 * task, and the followed one always leads.
 */
data class TaskRepositoryState(
    val availability: CoreUiAvailability,
    val generation: ULong,
    val task: TaskProjection?,
    val others: List<TaskProjection> = emptyList(),
    /**
     * The last control the browser would not take, or null.
     *
     * The control list on a [TaskProjection] is the core's, and the core knows
     * the task's state rather than the browser's authority over it. After a
     * browser restart the core restores a paused task still offering Resume —
     * its own rehydration calls that control "the inert state marker" — and
     * the consent that would honour it lived in the browser session that is
     * gone. Every surface used to drop that answer (decision 0221).
     */
    val controlRefusal: TaskControlRefusal? = null,
) {
    val tasks: List<TaskProjection>
        get() = listOfNotNull(task) + others
}

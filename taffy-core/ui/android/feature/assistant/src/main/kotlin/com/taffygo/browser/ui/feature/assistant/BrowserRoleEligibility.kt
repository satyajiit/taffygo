// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.task.TaskProjection

/**
 * Whether one exact accepted file is the source-verifiable value required before a role offer.
 *
 * Merely finishing a task is insufficient. The kept artifact must belong to a projection with
 * output facts, and every fact must still name at least one live source from that same projection.
 */
internal fun TaskProjection.qualifiesForBrowserRoleOffer(artifactId: String): Boolean {
    if (artifacts.none { it.id == artifactId && it.accepted }) return false
    if (sources.isEmpty() || facts.isEmpty()) return false
    val sourceIds = sources.mapTo(mutableSetOf()) { it.id }
    return facts.all { fact ->
        !fact.needsANewSource &&
            fact.sources.isNotEmpty() &&
            fact.sources.all(sourceIds::contains)
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import org.gradle.api.Project

/**
 * Attaches a verification task to every path that builds this module.
 *
 * Decision 0014 puts the module-graph and file-discipline checks in the fast
 * lane. A check that only runs under `check` is a check most builds skip, so
 * these hang off compilation as well: `assembleDebug`, `lintDebug`, and
 * `testDebugUnitTest` all reach them.
 */
internal fun Project.wireIntoEveryBuild(taskName: String) {
    tasks.matching { it.name in BUILD_ENTRY_POINTS }.configureEach { dependsOn(taskName) }
}

private val BUILD_ENTRY_POINTS = setOf("preBuild", "classes", "check")

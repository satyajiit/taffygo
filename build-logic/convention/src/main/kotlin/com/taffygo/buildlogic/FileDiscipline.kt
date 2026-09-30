// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import org.gradle.api.Project
import org.gradle.kotlin.dsl.register

/**
 * The soft cap from android-app-architecture section 5, in lines of code.
 *
 * The same number `tools/lib/file_discipline.py` applies to every other source
 * file in the repository. It is written twice because the two enforcers cannot
 * import from each other; it is the only number that is, and a change to one
 * without the other fails the repository-wide lane immediately.
 */
private const val MAX_KOTLIN_FILE_LINES = 400

/**
 * The exemption table shared with `tools/lib/file_discipline.py`, relative to
 * the repository root. One table, one meaning, read by both enforcers.
 */
private const val EXEMPTIONS_PATH = "tools/check.d/file-size-exemptions.tsv"

internal fun Project.configureFileDiscipline() {
    val discipline = tasks.register<FileDisciplineTask>("checkFileDiscipline") {
        group = "verification"
        description = "One public type per file and the soft line cap, in lines of code, on Kotlin sources."
        sources.from(fileTree("src") { include("**/*.kt") })
        maxLines.set(MAX_KOTLIN_FILE_LINES)
        repoRoot.set(rootProject.layout.projectDirectory)
        exemptions.set(rootProject.layout.projectDirectory.file(EXEMPTIONS_PATH))
        report.set(layout.buildDirectory.file("reports/file-discipline.txt"))
    }

    wireIntoEveryBuild(discipline.name)
}

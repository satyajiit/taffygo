// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import org.gradle.api.Project
import org.gradle.kotlin.dsl.register

internal fun Project.configureStringResources() {
    val projectPath = path
    val strings = tasks.register<StringResourceTask>("checkStringResources") {
        group = "verification"
        description = "Every user-visible string is externalized (parity row PAR-L10N-001)."
        // Shipped sources only: a test may write a literal, a screen may not.
        sources.from(fileTree("src/main") { include("**/*.kt") })
        stringResources.from(fileTree("src/main/res") { include("values/strings*.xml") })
        markupReferences.from(
            fileTree("src/main") {
                include("AndroidManifest.xml")
                include("res/**/*.xml")
                exclude("res/values/strings*.xml")
            },
        )
        modulePath.set(projectPath)
        report.set(layout.buildDirectory.file("reports/string-resources.txt"))
    }

    wireIntoEveryBuild(strings.name)
}

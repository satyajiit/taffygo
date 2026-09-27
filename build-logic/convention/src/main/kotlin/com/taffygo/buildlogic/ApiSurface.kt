// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import org.gradle.api.Project
import org.gradle.kotlin.dsl.register

/** The one package root the Android UI owns. Module packages hang off it. */
private const val ANDROID_UI_PACKAGE_ROOT = "com.taffygo.browser.ui"

internal fun Project.configureApiSurface() {
    val projectPath = path
    val projectPackage = androidUiPackageName()
    val apiSurface = tasks.register<ApiSurfaceTask>("checkApiSurface") {
        group = "verification"
        description = "Api/implementation separation: internal packages stay internal and stay private."
        sources.from(fileTree("src") { include("**/*.kt") })
        modulePath.set(projectPath)
        modulePackage.set(projectPackage)
        report.set(layout.buildDirectory.file("reports/api-surface.txt"))
    }

    wireIntoEveryBuild(apiSurface.name)
}

/**
 * The module's package, derived from its Gradle path so the two can never drift:
 * `:core:page-intelligence` owns `com.taffygo.browser.ui.core.page`.
 */
internal fun Project.androidUiPackageName(): String = when (path) {
    ":core:page-intelligence" -> "$ANDROID_UI_PACKAGE_ROOT.core.page"
    else -> path.removePrefix(":").split(':')
        .joinToString(".", prefix = "$ANDROID_UI_PACKAGE_ROOT.")
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import com.android.build.api.dsl.ApplicationExtension
import com.android.build.api.dsl.LibraryExtension
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.kotlin.dsl.dependencies

/**
 * Compose for the Taffy-owned surfaces. Applied on top of the application or
 * library convention. Decision 0024 gives TaffyGo every screen, superseding
 * 0003's bounded-island scope, so Compose owns the whole UI — but it still
 * never becomes a second source of truth for browser state: the browser
 * remains the authority and Compose projects it.
 */
class AndroidComposeConventionPlugin : Plugin<Project> {
    override fun apply(target: Project) = with(target) {
        pluginManager.apply("org.jetbrains.kotlin.plugin.compose")

        pluginManager.withPlugin("com.android.application") {
            extensions.configure(ApplicationExtension::class.java) {
                buildFeatures.compose = true
            }
        }
        pluginManager.withPlugin("com.android.library") {
            extensions.configure(LibraryExtension::class.java) {
                buildFeatures.compose = true
            }
        }

        dependencies {
            val bom = platform(libs.lib("compose-bom"))
            add("implementation", bom)
            add("androidTestImplementation", bom)

            add("implementation", libs.lib("compose-ui"))
            add("implementation", libs.lib("compose-ui-graphics"))
            add("implementation", libs.lib("compose-material3"))
            add("implementation", libs.lib("androidx-lifecycle-runtime-compose"))
            add("implementation", libs.lib("androidx-lifecycle-viewmodel-compose"))

            add("debugImplementation", libs.lib("compose-ui-tooling"))
            add("debugImplementation", libs.lib("compose-ui-tooling-preview"))
            add("debugImplementation", libs.lib("compose-ui-test-manifest"))

            add("androidTestImplementation", libs.lib("compose-ui-test-junit4"))
        }
    }
}

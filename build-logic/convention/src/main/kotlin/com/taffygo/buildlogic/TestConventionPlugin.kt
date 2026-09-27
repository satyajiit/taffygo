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
import org.gradle.api.tasks.testing.Test
import org.gradle.kotlin.dsl.dependencies

/**
 * The JVM test contract every module shares: pure reducers and injected
 * dispatchers keep the domain and presentation layers runnable without a
 * device (android-app-architecture section 9).
 *
 * A pure-JVM module has no `testDebugUnitTest`, because it has no variants. It
 * still has to run in the fast lane, so this plugin gives it a task of that
 * name that runs its tests. Otherwise the UI host would have a module whose
 * tests the documented command silently skips.
 */
class TestConventionPlugin : Plugin<Project> {
    override fun apply(target: Project) = with(target) {
        dependencies {
            add("testImplementation", libs.lib("junit"))
            add("testImplementation", libs.lib("kotlinx-coroutines-test"))
            add("testImplementation", libs.lib("turbine"))
        }

        pluginManager.withPlugin("com.android.base") {
            dependencies {
                add("androidTestImplementation", libs.lib("androidx-test-ext-junit"))
                // The class every module's testInstrumentationRunner names.
                // ext-junit does not carry it, so a test APK without this
                // dependency crashes on launch with ClassNotFoundException
                // before a single test runs.
                add("androidTestImplementation", libs.lib("androidx-test-runner"))
                add("androidTestImplementation", libs.lib("kotlinx-coroutines-test"))
            }
        }

        pluginManager.withPlugin("com.android.application") {
            extensions.configure(ApplicationExtension::class.java) {
                testOptions.unitTests.isReturnDefaultValues = true
            }
        }
        pluginManager.withPlugin("com.android.library") {
            extensions.configure(LibraryExtension::class.java) {
                testOptions.unitTests.isReturnDefaultValues = true
            }
        }

        pluginManager.withPlugin("org.jetbrains.kotlin.jvm") {
            tasks.register("testDebugUnitTest") {
                group = "verification"
                description = "Runs this pure-JVM module's tests under the name the fast lane uses."
                dependsOn("test")
            }
        }

        tasks.withType(Test::class.java).configureEach {
            testLogging {
                events("failed", "skipped")
            }
        }
    }
}

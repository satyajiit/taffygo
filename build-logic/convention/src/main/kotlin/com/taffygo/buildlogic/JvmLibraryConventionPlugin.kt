// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import com.android.build.api.dsl.Lint
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.api.plugins.JavaPluginExtension
import org.gradle.jvm.toolchain.JavaLanguageVersion

/**
 * Pure Kotlin modules with no Android dependency. `:core:model` uses it so the
 * domain layer cannot accidentally reach an Android type.
 *
 * Lint runs here too. Without the standalone lint plugin a pure-JVM module is
 * treated as an external dependency and its sources are never analysed, which
 * would leave one module of the UI host outside the gate the others are held
 * to. Its task is named `lint`, so this plugin also gives it the variant-shaped
 * name the fast lane uses.
 */
class JvmLibraryConventionPlugin : Plugin<Project> {
    override fun apply(target: Project) = with(target) {
        pluginManager.apply("org.jetbrains.kotlin.jvm")
        pluginManager.apply("com.android.lint")
        pluginManager.apply("taffygo.test")

        extensions.configure(JavaPluginExtension::class.java) {
            toolchain.languageVersion.set(JavaLanguageVersion.of(libs.version("jvmToolchain")))
        }

        extensions.configure(Lint::class.java) {
            warningsAsErrors = true
            abortOnError = true
        }

        tasks.register("lintDebug") {
            group = "verification"
            description = "Runs this pure-JVM module's lint under the name the fast lane uses."
            dependsOn("lint")
        }

        configureKotlin()
        configureFileDiscipline()
        configureApiSurface()
        configureModuleGraphRules()
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import com.android.build.api.dsl.LibraryExtension
import org.gradle.api.JavaVersion
import org.gradle.api.Plugin
import org.gradle.api.Project

/**
 * Every `:core:*` and `:feature:*` module that needs Android APIs. Library
 * modules carry no application identity and no launcher entry point.
 */
class AndroidLibraryConventionPlugin : Plugin<Project> {
    override fun apply(target: Project) = with(target) {
        pluginManager.apply("com.android.library")
        // Kotlin is built into AGP from 9.0; applying
        // org.jetbrains.kotlin.android is a hard error. See the rule
        // recorded in gradle/libs.versions.toml.
        pluginManager.apply("taffygo.test")

        extensions.configure(LibraryExtension::class.java) {
            compileSdk = libs.version("compileSdk").toInt()

            defaultConfig {
                minSdk = libs.version("minSdk").toInt()
                testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
                consumerProguardFiles("consumer-rules.pro")
            }

            compileOptions {
                sourceCompatibility = JavaVersion.toVersion(libs.version("jvmToolchain"))
                targetCompatibility = JavaVersion.toVersion(libs.version("jvmToolchain"))
            }

            lint {
                warningsAsErrors = true
                abortOnError = true
                disable += setOf("GradleDependency", "AndroidGradlePluginVersion", "NewerVersionAvailable")
            }
        }

        configureKotlin()
        configureFileDiscipline()
        configureApiSurface()
        configureStringResources()
        configureModuleGraphRules()
    }
}

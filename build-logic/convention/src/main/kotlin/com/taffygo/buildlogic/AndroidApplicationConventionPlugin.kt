// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import com.android.build.api.dsl.ApplicationExtension
import org.gradle.api.JavaVersion
import org.gradle.api.Plugin
import org.gradle.api.Project

/**
 * The UI host application shell. Only `:app` uses it: one thin module that
 * owns the dependency-injection graph root, the startup graph, and the nav
 * host (android-app-architecture section 5).
 */
class AndroidApplicationConventionPlugin : Plugin<Project> {
    override fun apply(target: Project) = with(target) {
        pluginManager.apply("com.android.application")
        // Kotlin is built into AGP from 9.0; applying
        // org.jetbrains.kotlin.android is a hard error. See the rule
        // recorded in gradle/libs.versions.toml.
        pluginManager.apply("taffygo.test")

        extensions.configure(ApplicationExtension::class.java) {
            compileSdk = libs.version("compileSdk").toInt()

            defaultConfig {
                minSdk = libs.version("minSdk").toInt()
                targetSdk = libs.version("targetSdk").toInt()
                versionCode = 1
                versionName = "0.0.1"
                testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
            }

            buildFeatures {
                buildConfig = true
            }

            buildTypes {
                getByName("debug") {
                    // AGP supplies its standard local debug signing identity.
                    // No repository credential or custom key is consulted.
                    //
                    // Parity row PAR-L10N-001: the debug package carries the
                    // pseudo-localized variants, so an accented, expanded,
                    // bracketed build is one device-language change away.
                    isPseudoLocalesEnabled = true
                }
                getByName("release") {
                    isMinifyEnabled = true
                    isShrinkResources = true
                    proguardFiles(
                        getDefaultProguardFile("proguard-android-optimize.txt"),
                        "proguard-rules.pro",
                    )
                    // Deliberately unsigned. A release candidate is signed only
                    // by the external release job after repository verification;
                    // this build never falls back to a debug or committed key.
                }
            }

            compileOptions {
                sourceCompatibility = JavaVersion.toVersion(libs.version("jvmToolchain"))
                targetCompatibility = JavaVersion.toVersion(libs.version("jvmToolchain"))
            }

            lint {
                warningsAsErrors = true
                abortOnError = true
                checkDependencies = true
                disable += setOf("GradleDependency", "AndroidGradlePluginVersion", "NewerVersionAvailable")
            }

            packaging {
                resources {
                    excludes += "/META-INF/{AL2.0,LGPL2.1}"
                }
            }
        }

        configureKotlin()
        configureFileDiscipline()
        configureApiSurface()
        configureStringResources()
        configureModuleGraphRules()
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import org.gradle.api.artifacts.ExternalModuleDependencyBundle
import org.gradle.api.provider.Provider
import org.gradle.plugin.use.PluginDependency

plugins {
    `kotlin-dsl`
}

group = "com.taffygo.buildlogic"

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(libs.versions.jvmToolchain.get()))
    }
}

// A plugin marker resolves to the plugin implementation artifact, which keeps
// every version in gradle/libs.versions.toml instead of repeating coordinates.
fun Provider<PluginDependency>.marker(): Provider<String> =
    map { "${it.pluginId}:${it.pluginId}.gradle.plugin:${it.version}" }

dependencies {
    compileOnly(libs.plugins.android.application.marker())
    // The Kotlin Gradle plugin API — KotlinBaseExtension, KotlinCompile, the
    // compiler-option DSL — that the shared conventions compile against. It
    // comes from the JVM plugin marker rather than a Kotlin Android one:
    // Kotlin is built into the Android Gradle Plugin from AGP 9.0, so no
    // Kotlin Android plugin exists to depend on. See gradle/libs.versions.toml.
    compileOnly(libs.plugins.kotlin.jvm.marker())
    compileOnly(libs.plugins.kotlin.compose.marker())
    compileOnly(libs.plugins.ksp.marker())
}

gradlePlugin {
    plugins {
        register("androidApplication") {
            id = "taffygo.android.application"
            implementationClass = "com.taffygo.buildlogic.AndroidApplicationConventionPlugin"
        }
        register("androidLibrary") {
            id = "taffygo.android.library"
            implementationClass = "com.taffygo.buildlogic.AndroidLibraryConventionPlugin"
        }
        register("androidFeature") {
            id = "taffygo.android.feature"
            implementationClass = "com.taffygo.buildlogic.AndroidFeatureConventionPlugin"
        }
        register("androidCompose") {
            id = "taffygo.android.compose"
            implementationClass = "com.taffygo.buildlogic.AndroidComposeConventionPlugin"
        }
        register("dagger") {
            id = "taffygo.dagger"
            implementationClass = "com.taffygo.buildlogic.DaggerConventionPlugin"
        }
        register("jvmLibrary") {
            id = "taffygo.jvm.library"
            implementationClass = "com.taffygo.buildlogic.JvmLibraryConventionPlugin"
        }
        register("test") {
            id = "taffygo.test"
            implementationClass = "com.taffygo.buildlogic.TestConventionPlugin"
        }
    }
}

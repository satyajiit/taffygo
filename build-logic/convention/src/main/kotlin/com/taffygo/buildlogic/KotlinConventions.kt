// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import org.gradle.api.Project
import org.gradle.jvm.toolchain.JavaLanguageVersion
import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import org.jetbrains.kotlin.gradle.dsl.KotlinBaseExtension
import org.jetbrains.kotlin.gradle.dsl.KotlinJvmCompilerOptions
import org.jetbrains.kotlin.gradle.tasks.KotlinCompile

/**
 * Kotlin settings shared by every module: the pinned JVM toolchain, explicit
 * opt-ins, and warnings that must not silently accumulate.
 */
internal fun Project.configureKotlin() {
    val jdk = libs.version("jvmToolchain")

    extensions.configure(KotlinBaseExtension::class.java) {
        jvmToolchain { languageVersion.set(JavaLanguageVersion.of(jdk)) }
    }

    tasks.withType(KotlinCompile::class.java).configureEach {
        compilerOptions {
            configureCommonKotlinOptions(jdk)
        }
    }
}

private fun KotlinJvmCompilerOptions.configureCommonKotlinOptions(jdk: String) {
    jvmTarget.set(JvmTarget.fromTarget(jdk))
    freeCompilerArgs.addAll(
        // Coroutine test helpers and the lifecycle-aware collectors are opt-in.
        "-opt-in=kotlin.RequiresOptIn",
    )
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import com.android.build.api.dsl.LibraryExtension
import org.gradle.api.Project
import org.gradle.kotlin.dsl.configure

/**
 * Mounts a committed generated Kotlin directory into this module's `main`
 * source set, named relative to the repository root.
 *
 * There is one way to do this because there were two, and they had drifted:
 * `:core:api` called `kotlin.srcDir(Directory)` while `:core:designsystem`
 * called `kotlin.directories.add(String)` with an `.asFile.absolutePath`
 * conversion. Both work, which is precisely why the difference survived — a
 * reader could not tell whether it meant anything.
 *
 * A generated directory is mounted rather than copied because GN compiles the
 * same files for the product from the same path; the generator is the single
 * owner and neither build may hold its own copy.
 */
fun Project.mountGeneratedKotlin(repositoryRelativePath: String) {
    val directory = rootProject.layout.projectDirectory.dir(repositoryRelativePath)
    extensions.configure<LibraryExtension> {
        sourceSets.getByName("main") {
            kotlin.srcDir(directory)
        }
    }
}

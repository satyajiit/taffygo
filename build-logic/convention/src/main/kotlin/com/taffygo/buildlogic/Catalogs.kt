// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import org.gradle.api.Project
import org.gradle.api.artifacts.VersionCatalog
import org.gradle.api.artifacts.VersionCatalogsExtension
import org.gradle.kotlin.dsl.getByType

/**
 * The single version catalog, `gradle/libs.versions.toml`. Convention plugins
 * read every coordinate from here; no module and no plugin repeats a version.
 */
internal val Project.libs: VersionCatalog
    get() = extensions.getByType<VersionCatalogsExtension>().named("libs")

internal fun VersionCatalog.version(alias: String): String =
    findVersion(alias).orElseThrow {
        IllegalStateException("gradle/libs.versions.toml is missing version '$alias'")
    }.requiredVersion

internal fun VersionCatalog.lib(alias: String) =
    findLibrary(alias).orElseThrow {
        IllegalStateException("gradle/libs.versions.toml is missing library '$alias'")
    }

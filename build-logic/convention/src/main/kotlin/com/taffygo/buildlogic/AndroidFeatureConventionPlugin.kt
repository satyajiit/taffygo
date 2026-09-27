// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.kotlin.dsl.dependencies

/**
 * A `:feature:*` module. Every feature has the same shape, so it is described
 * once here rather than copied into four build scripts — the rule
 * android-app-architecture section 5 states as "convention plugins own all
 * shared build config".
 *
 * Shared Android/Compose/Dagger mechanics live here. Project edges stay in the
 * generated component graph so each feature receives only the ports it names.
 */
class AndroidFeatureConventionPlugin : Plugin<Project> {
    override fun apply(target: Project) = with(target) {
        pluginManager.apply("taffygo.android.library")
        pluginManager.apply("taffygo.android.compose")
        pluginManager.apply("taffygo.dagger")

        dependencies {
            add("implementation", libs.lib("androidx-lifecycle-runtime-ktx"))
        }
    }
}

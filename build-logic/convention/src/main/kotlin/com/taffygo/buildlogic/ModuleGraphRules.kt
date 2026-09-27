// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import org.gradle.api.Project
import org.gradle.api.artifacts.ProjectDependency
import org.gradle.kotlin.dsl.dependencies
import org.gradle.kotlin.dsl.register

/**
 * Wires [ModuleGraphTask] into the module. The dependency paths are read once,
 * at configuration time, and handed to the task as plain strings so the task
 * itself holds no reference to the Gradle model and the configuration cache
 * stays usable.
 */
internal fun Project.configureModuleGraphRules() {
    val projectPath = path
    val table = isolated.rootProject.projectDirectory.file(GeneratedModuleGraph.TABLE)
    val module = GeneratedModuleGraph.load(table.asFile).requireModule(projectPath)

    dependencies {
        module.apiDependencies.forEach { dependency ->
            add("api", project(mapOf("path" to dependency)))
        }
        module.implementationDependencies.forEach { dependency ->
            add("implementation", project(mapOf("path" to dependency)))
        }
    }

    val graph = tasks.register<ModuleGraphTask>("checkModuleGraph") {
        group = "verification"
        description = "Exact generated project dependencies and API visibility."
        modulePath.set(projectPath)
        graphTable.set(table)
        report.set(layout.buildDirectory.file("reports/module-graph.txt"))
    }

    afterEvaluate {
        fun projectDependencies(configuration: String): Set<String> = configurations
            .findByName(configuration)
            ?.dependencies
            ?.withType(ProjectDependency::class.java)
            ?.map { it.path }
            ?.toSet()
            .orEmpty()

        val otherDependencies = FORBIDDEN_PROJECT_CONFIGURATIONS.flatMap { configuration ->
            projectDependencies(configuration).map { dependency -> "$configuration:$dependency" }
        }
        graph.configure {
            apiProjectDependencies.set(projectDependencies("api"))
            implementationProjectDependencies.set(projectDependencies("implementation"))
            forbiddenProjectDependencies.set(otherDependencies)
        }
    }

    wireIntoEveryBuild(graph.name)
}

private val FORBIDDEN_PROJECT_CONFIGURATIONS = listOf(
    "compileOnly",
    "runtimeOnly",
    "testImplementation",
    "androidTestImplementation",
)

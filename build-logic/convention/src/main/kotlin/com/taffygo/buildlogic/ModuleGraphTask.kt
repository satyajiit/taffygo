// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import org.gradle.api.DefaultTask
import org.gradle.api.GradleException
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.provider.Property
import org.gradle.api.provider.SetProperty
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputFile
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction

/**
 * Compares the configured project graph with the exact generated projection.
 * It runs before compilation, so a hand-written, missing, or misclassified
 * edge fails the build that introduced it.
 */
abstract class ModuleGraphTask : DefaultTask() {

    /** The Gradle path of the module being checked. */
    @get:Input
    abstract val modulePath: Property<String>

    /** Project dependencies configured on the public API surface. */
    @get:Input
    abstract val apiProjectDependencies: SetProperty<String>

    /** Project dependencies configured as implementation details. */
    @get:Input
    abstract val implementationProjectDependencies: SetProperty<String>

    /** Project edges on configurations the manifest cannot represent. */
    @get:Input
    abstract val forbiddenProjectDependencies: SetProperty<String>

    /** The generated graph. An input, so a manifest edit re-runs the check. */
    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val graphTable: RegularFileProperty

    /** The record of what was checked, so a passing run is still evidence. */
    @get:OutputFile
    abstract val report: RegularFileProperty

    @TaskAction
    fun check() {
        val consumer = modulePath.get()
        val module = GeneratedModuleGraph.load(graphTable.get().asFile).requireModule(consumer)
        val actualApi = apiProjectDependencies.get()
        val actualImplementation = implementationProjectDependencies.get()
        val findings = mutableListOf<String>()
        findings += differences("api", module.apiDependencies, actualApi)
        findings += differences(
            "implementation",
            module.implementationDependencies,
            actualImplementation,
        )
        forbiddenProjectDependencies.get().sorted().forEach { edge ->
            findings += "$consumer declares project edge $edge outside api/implementation"
        }

        report.get().asFile.apply {
            parentFile.mkdirs()
            writeText(
                buildString {
                    appendLine("module: $consumer")
                    appendLine("api: ${actualApi.sorted().joinToString().ifEmpty { "none" }}")
                    appendLine(
                        "implementation: " +
                            actualImplementation.sorted().joinToString().ifEmpty { "none" },
                    )
                    appendLine(if (findings.isEmpty()) "verdict: exact" else "verdict: mismatch")
                    findings.forEach { appendLine("  $it") }
                },
            )
        }

        if (findings.isNotEmpty()) {
            throw GradleException(
                "Module-graph findings (android-app-architecture section 5):\n" +
                    findings.joinToString("\n") { "  $it" },
            )
        }
    }

    private fun differences(
        configuration: String,
        expected: Set<String>,
        actual: Set<String>,
    ): List<String> = buildList {
        (expected - actual).sorted().forEach { dependency ->
            add("${modulePath.get()} is missing generated $configuration edge to $dependency")
        }
        (actual - expected).sorted().forEach { dependency ->
            add("${modulePath.get()} has undeclared $configuration edge to $dependency")
        }
    }
}

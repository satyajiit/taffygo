// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import java.io.File
import org.gradle.api.DefaultTask
import org.gradle.api.GradleException
import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.CacheableTask
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputFiles
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction

/**
 * Executable form of "each `:core` module exposes an api surface;
 * implementation details are `internal`" (android-app-architecture section 5,
 * decision 0014 item 3).
 *
 * Three rules, all mechanical:
 *
 * 1. A top-level declaration inside a package named `internal` is `internal` or
 *    `private`. An implementation package that leaks a public type is not an
 *    implementation package.
 * 2. No module reaches into another module's `internal` package.
 * A pure port or model module may intentionally have no implementation package;
 * platform adapters are owned by the component that supplies the port.
 */
@CacheableTask
abstract class ApiSurfaceTask : DefaultTask() {

    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val sources: ConfigurableFileCollection

    /** The Gradle path of the module being checked. */
    @get:Input
    abstract val modulePath: Property<String>

    /** This module's own package prefix, which its own sources may import. */
    @get:Input
    abstract val modulePackage: Property<String>

    @get:OutputFile
    abstract val report: RegularFileProperty

    @TaskAction
    fun check() {
        val ownPackage = modulePackage.get()
        val files = sources.files.filter { it.isFile && it.name.endsWith(".kt") }.sortedBy { it.path }

        val findings = mutableListOf<String>()

        for (file in files) {
            val lines = file.readLines()
            val declaredPackage = lines.firstNotNullOfOrNull { line ->
                PACKAGE.find(line)?.groupValues?.getOrNull(1)
            } ?: continue

            val inInternalPackage = declaredPackage.split('.').contains(INTERNAL_SEGMENT)
            if (inInternalPackage) {
                findings += leakedDeclarations(file, lines)
            }
            findings += foreignInternalImports(file, lines, ownPackage)
        }

        report.get().asFile.apply {
            parentFile.mkdirs()
            writeText(
                if (findings.isEmpty()) {
                    "module: ${modulePath.get()}\napi surface: clean (${files.size} files)\n"
                } else {
                    findings.joinToString("\n", postfix = "\n")
                },
            )
        }

        if (findings.isNotEmpty()) {
            throw GradleException(
                "Api-surface findings (android-app-architecture section 5):\n" +
                    findings.joinToString("\n") { "  $it" },
            )
        }
    }

    private fun leakedDeclarations(file: File, lines: List<String>): List<String> =
        lines.withIndex().mapNotNull { (index, line) ->
            val name = TOP_LEVEL_DECLARATION.find(line)?.groupValues?.getOrNull(2) ?: return@mapNotNull null
            "${file.path}:${index + 1}: `$name` is public inside an implementation package; " +
                "mark it internal."
        }

    private fun foreignInternalImports(file: File, lines: List<String>, ownPackage: String): List<String> =
        lines.withIndex().mapNotNull { (index, line) ->
            val imported = FOREIGN_INTERNAL_IMPORT.find(line)?.groupValues?.getOrNull(1)
                ?: return@mapNotNull null
            if (imported.startsWith("$ownPackage.")) return@mapNotNull null
            "${file.path}:${index + 1}: imports `$imported`, another module's implementation package."
        }

    private companion object {
        const val INTERNAL_SEGMENT = "internal"

        val PACKAGE = Regex("^package\\s+([A-Za-z0-9_.]+)")

        /**
         * A top-level declaration that is neither `internal` nor `private`.
         * Indented declarations are members of an enclosing type and take their
         * visibility from it, so the anchor matters.
         *
         * Three shapes used to slip past this and are now covered, because a
         * leak the check cannot see is worse than no check: a same-line
         * annotation (`@Composable fun Card()`) broke the anchor, and `const`
         * and `lateinit` were absent from the modifier set, so a public
         * `const val` — a compile-time constant that crosses the module
         * boundary by value — read as no declaration at all. `fun` is admitted
         * as a modifier only before `interface`, so a plain `fun` declaration
         * still reports its own name.
         */
        val TOP_LEVEL_DECLARATION = Regex(
            "^(?:@[A-Za-z_][A-Za-z0-9_.]*(?:\\([^)]*\\))?\\s+)*" +
                "(?:public\\s+)?(?:(?:abstract|final|open|sealed|data|value|inline|annotation|enum|" +
                "expect|actual|external|suspend|operator|infix|tailrec|const|lateinit|" +
                "fun(?=\\s+interface))\\s+)*" +
                "(class|interface|object|fun|val|var|typealias)\\s+" +
                "(?:<[^>]*>\\s+)?([A-Za-z_][A-Za-z0-9_]*)",
        )

        val FOREIGN_INTERNAL_IMPORT = Regex(
            "^import\\s+(com\\.taffygo\\.browser\\.ui\\.[A-Za-z0-9_.]*\\.$INTERNAL_SEGMENT)\\.",
        )
    }
}

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
 * Executable form of the file-discipline rule in android-app-architecture
 * section 5: one public type per file and a soft line cap per file. A file that
 * grows past the cap is a design smell, so the build says so instead of a
 * reviewer having to notice.
 *
 * The cap is measured the way `tools/lib/code_lines.py` measures it — blank
 * lines, comments and inline test blocks excluded — because
 * `./tools/check fast` applies the same rule to the same files and two
 * enforcers that measured differently would disagree about a file nobody
 * changed. A cap that counted comments would also be a cap on explanation,
 * and the first thing anyone would do to get under it is delete the paragraph
 * that says why the code is the way it is.
 */
@CacheableTask
abstract class FileDisciplineTask : DefaultTask() {

    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val sources: ConfigurableFileCollection

    @get:Input
    abstract val maxLines: Property<Int>

    @get:OutputFile
    abstract val report: RegularFileProperty

    @TaskAction
    fun check() {
        val cap = maxLines.get()
        val findings = sources.files
            .filter { it.isFile && it.name.endsWith(".kt") }
            .sortedBy { it.path }
            .flatMap { inspect(it, cap) }

        report.get().asFile.apply {
            parentFile.mkdirs()
            writeText(if (findings.isEmpty()) "clean\n" else findings.joinToString("\n", postfix = "\n"))
        }

        if (findings.isNotEmpty()) {
            throw GradleException(
                "File discipline findings (android-app-architecture section 5):\n" +
                    findings.joinToString("\n") { "  $it" },
            )
        }
    }

    private fun inspect(file: File, cap: Int): List<String> {
        val lines = file.readLines()
        val findings = mutableListOf<String>()

        val code = codeLines(lines)
        if (code > cap) {
            findings += "${file.path}: $code lines of code exceeds the $cap-line cap. " +
                "Split it along a responsibility seam, or add an entry with a reason " +
                "and a ceiling to tools/check.d/file-size-exemptions.tsv."
        }

        val declared = lines
            .filter { PUBLIC_TYPE.containsMatchIn(it) }
            .mapNotNull { PUBLIC_TYPE.find(it)?.groupValues?.getOrNull(2) }
        if (declared.size > 1) {
            findings += "${file.path}: declares ${declared.size} public types " +
                "(${declared.joinToString()}); one public type per file."
        }

        return findings
    }

    /** Lines that are neither blank nor comment, block comments included. */
    private fun codeLines(lines: List<String>): Int {
        var total = 0
        var inBlock = false
        for (raw in lines) {
            var text = raw
            if (inBlock) {
                val close = text.indexOf("*/")
                if (close < 0) continue
                text = text.substring(close + 2)
                inBlock = false
            }
            while (true) {
                val open = text.indexOf("/*")
                if (open < 0) break
                val close = text.indexOf("*/", open + 2)
                if (close < 0) {
                    text = text.substring(0, open)
                    inBlock = true
                    break
                }
                text = text.substring(0, open) + text.substring(close + 2)
            }
            val stripped = text.trim()
            if (stripped.isEmpty() || stripped.startsWith("//")) continue
            total++
        }
        return total
    }

    private companion object {
        /** A top-level, non-internal, non-private type declaration. */
        val PUBLIC_TYPE = Regex(
            "^(?:public\\s+)?(?:(?:abstract|final|open|sealed|data|value|inline|annotation|enum|fun)\\s+)*" +
                "(class|interface|object)\\s+([A-Za-z_][A-Za-z0-9_]*)",
        )
    }
}

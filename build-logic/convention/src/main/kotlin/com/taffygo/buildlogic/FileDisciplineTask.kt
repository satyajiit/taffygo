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
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.CacheableTask
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputFile
import org.gradle.api.tasks.InputFiles
import org.gradle.api.tasks.Internal
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
 *
 * A file listed in `tools/check.d/file-size-exemptions.tsv` may grow to that
 * row's own ceiling instead of the default cap, matched by relative path the
 * same way `tools/lib/file_discipline.py` matches it with Python's `fnmatch` —
 * so one row means the same thing to both tools. A malformed or dead row is
 * not this task's problem to reject; the Python lane already owns that, and
 * this task only has to honour the rows that are there.
 */
@CacheableTask
abstract class FileDisciplineTask : DefaultTask() {

    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val sources: ConfigurableFileCollection

    @get:Input
    abstract val maxLines: Property<Int>

    /** The repository root, used only to compute each file's relative path for
     * exemption matching. Not an input in its own right: this task already
     * depends on [exemptions] for that table's content, and depending on the
     * whole repository tree here would invalidate the cache on any file
     * changing anywhere. */
    @get:Internal
    abstract val repoRoot: DirectoryProperty

    @get:InputFile
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val exemptions: RegularFileProperty

    @get:OutputFile
    abstract val report: RegularFileProperty

    @TaskAction
    fun check() {
        val cap = maxLines.get()
        val root = repoRoot.get().asFile
        val table = loadExemptions(exemptions.get().asFile)
        val findings = sources.files
            .filter { it.isFile && it.name.endsWith(".kt") }
            .sortedBy { it.path }
            .flatMap { inspect(it, cap, root, table) }

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

    private fun inspect(file: File, defaultCap: Int, root: File, table: List<Exemption>): List<String> {
        val lines = file.readLines()
        val findings = mutableListOf<String>()

        val relative = file.relativeTo(root).path.replace(File.separatorChar, '/')
        val exemption = table.firstOrNull { it.matches(relative) }

        val code = codeLines(lines)
        if (exemption != null) {
            if (code > exemption.ceiling) {
                findings += "${file.path}: $code lines of code, past its own exemption ceiling of " +
                    "${exemption.ceiling} (${exemption.reason}). The exemption said this file had " +
                    "stopped growing; it has not. Split it."
            }
        } else if (code > defaultCap) {
            findings += "${file.path}: $code lines of code exceeds the $defaultCap-line cap. " +
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

/** One row of `tools/check.d/file-size-exemptions.tsv`: a path glob, the
 * largest the matching file may be in lines of code, and its reason. */
private data class Exemption(val glob: String, val ceiling: Int, val reason: String) {
    private val pattern: Regex = globToRegex(glob)
    fun matches(relative: String): Boolean = pattern.matches(relative)
}

/**
 * The exemption table, or an empty list if it doesn't exist yet.
 *
 * Rejecting a malformed or dead row is `tools/lib/file_discipline.py`'s job,
 * enforced in the same `./tools/check fast` run this task is part of. A row
 * this parser cannot make sense of is simply not honoured here — it does not
 * fail the Gradle build, since the Python lane already fails it.
 */
private fun loadExemptions(file: File): List<Exemption> {
    if (!file.isFile) return emptyList()
    val exemptions = mutableListOf<Exemption>()
    for (raw in file.readLines()) {
        val line = raw.trimEnd('\r')
        val trimmed = line.trimStart()
        if (trimmed.isEmpty() || trimmed.startsWith("#")) continue
        val parts = line.split("\t")
        if (parts.size != 3) continue
        val (glob, ceilingText, reason) = parts
        val ceiling = ceilingText.toIntOrNull() ?: continue
        if (reason.isBlank()) continue
        exemptions += Exemption(glob, ceiling, reason)
    }
    return exemptions
}

/**
 * Translates a shell-style glob (`*`, `?`, `[...]`) into a [Regex] that
 * matches a relative path the same way Python's `fnmatch.fnmatch` would,
 * for the pattern vocabulary this table actually uses.
 */
private fun globToRegex(glob: String): Regex {
    val sb = StringBuilder()
    var i = 0
    while (i < glob.length) {
        when (val c = glob[i]) {
            '*' -> sb.append(".*")
            '?' -> sb.append(".")
            '[' -> {
                val close = glob.indexOf(']', i + 1)
                if (close < 0) {
                    sb.append("\\[")
                } else {
                    var inner = glob.substring(i + 1, close)
                    if (inner.startsWith("!")) inner = "^" + inner.substring(1)
                    sb.append('[').append(inner).append(']')
                    i = close
                }
            }
            else -> sb.append(Regex.escape(c.toString()))
        }
        i++
    }
    return Regex(sb.toString())
}
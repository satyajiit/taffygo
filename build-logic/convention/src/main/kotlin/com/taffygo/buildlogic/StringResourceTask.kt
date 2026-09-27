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
import org.gradle.api.tasks.Optional
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction

/**
 * Executable form of parity row PAR-L10N-001: every string is externalized.
 *
 * The Android lint check for hard-coded text reads XML layouts, and TaffyGo has
 * none — Compose is the binding layer (decision 0014 item 2). So the rule is
 * enforced here instead, over the four ways a literal reaches a person in a
 * Compose tree, plus the resource hygiene that makes a translation possible:
 *
 * 1. No user-visible string literal in Kotlin.
 * 2. No duplicate string name inside a module.
 * 3. Every `R.string` name a module uses is declared by that module, so a
 *    module's strings travel with it.
 * 4. Every string a module declares is used by that module.
 * 5. A string with more than one argument numbers them, because a translation
 *    reorders arguments.
 */
@CacheableTask
abstract class StringResourceTask : DefaultTask() {

    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val sources: ConfigurableFileCollection

    @get:InputFiles
    @get:Optional
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val stringResources: ConfigurableFileCollection

    /**
     * The manifest and the other resource files, which reference a string as
     * `@string/name` rather than through generated code.
     */
    @get:InputFiles
    @get:Optional
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val markupReferences: ConfigurableFileCollection

    @get:Input
    abstract val modulePath: Property<String>

    @get:OutputFile
    abstract val report: RegularFileProperty

    @TaskAction
    fun check() {
        val kotlinFiles = sources.files.filter { it.isFile && it.name.endsWith(".kt") }.sortedBy { it.path }
        val findings = mutableListOf<String>()

        val declared = mutableMapOf<String, String>()
        for (resource in stringResources.files.filter { it.isFile }.sortedBy { it.path }) {
            for ((name, value) in parseResources(resource)) {
                val previous = declared.put(name, value)
                if (previous != null) {
                    findings += "${resource.path}: `$name` is declared twice."
                }
                if (UNNUMBERED_ARGUMENT.findAll(value).count() > 1) {
                    findings += "${resource.path}: `$name` has more than one unnumbered argument; " +
                        "number them (%1\$s, %2\$s) so a translation can reorder them."
                }
            }
        }

        val used = mutableSetOf<String>()
        for (file in kotlinFiles) {
            file.readLines().forEachIndexed { index, rawLine ->
                val line = stripComment(rawLine)
                STRING_REFERENCE.findAll(line).forEach { used += it.groupValues[1] }
                PLURALS_REFERENCE.findAll(line).forEach { used += it.groupValues[1] }
                LITERAL_IN_UI.find(line)?.let {
                    findings += "${file.path}:${index + 1}: user-visible literal " +
                        "`${it.groupValues[2]}`; move it to strings.xml."
                }
            }
        }

        for (markup in markupReferences.files.filter { it.isFile }) {
            MARKUP_REFERENCE.findAll(markup.readText()).forEach { used += it.groupValues[1] }
        }

        (used - declared.keys).sorted().forEach {
            findings += "${modulePath.get()}: uses R.string.$it, which this module does not declare."
        }
        (declared.keys - used).sorted().forEach {
            findings += "${modulePath.get()}: declares `$it`, which this module never uses."
        }

        report.get().asFile.apply {
            parentFile.mkdirs()
            writeText(
                if (findings.isEmpty()) {
                    "module: ${modulePath.get()}\nstrings declared: ${declared.size}\nstrings used: ${used.size}\n"
                } else {
                    findings.joinToString("\n", postfix = "\n")
                },
            )
        }

        if (findings.isNotEmpty()) {
            throw GradleException(
                "String-externalization findings (parity row PAR-L10N-001):\n" +
                    findings.joinToString("\n") { "  $it" },
            )
        }
    }

    /**
     * Every named string, and every named plural with its quantity forms joined
     * so an argument check sees them all.
     */
    private fun parseResources(file: File): List<Pair<String, String>> {
        val text = file.readText()
        val strings = STRING_ELEMENT.findAll(text).map { it.groupValues[1] to it.groupValues[2] }
        val plurals = PLURALS_ELEMENT.findAll(text).map { it.groupValues[1] to it.groupValues[2] }
        return (strings + plurals).toList()
    }

    private fun stripComment(line: String): String {
        val trimmed = line.trimStart()
        if (trimmed.startsWith("//") || trimmed.startsWith("*") || trimmed.startsWith("/*")) return ""
        val comment = line.indexOf("//")
        return if (comment >= 0) line.substring(0, comment) else line
    }

    private companion object {
        val STRING_ELEMENT = Regex("<string\\s+name=\"([^\"]+)\"[^>]*>(.*?)</string>", RegexOption.DOT_MATCHES_ALL)
        val STRING_REFERENCE = Regex("R\\.string\\.([A-Za-z0-9_]+)")

        /** A reference from a manifest or another resource file. */
        val MARKUP_REFERENCE = Regex("@(?:string|plurals)/([A-Za-z0-9_]+)")

        val PLURALS_ELEMENT = Regex(
            "<plurals\\s+name=\"([^\"]+)\"[^>]*>(.*?)</plurals>",
            RegexOption.DOT_MATCHES_ALL,
        )
        val PLURALS_REFERENCE = Regex("R\\.plurals\\.([A-Za-z0-9_]+)")

        /** `%s` or `%d` with no position, which a translation cannot reorder. */
        val UNNUMBERED_ARGUMENT = Regex("%[-#+ 0,(]*\\d*(?:\\.\\d+)?[sdfx]")

        /**
         * The four places a literal becomes user-visible text in a Compose
         * tree: the first argument of `Text`, a content description, a
         * `text =` argument, and a `label =` argument.
         */
        val LITERAL_IN_UI = Regex(
            "(\\bText\\(\\s*|contentDescription\\s*=\\s*|\\btext\\s*=\\s*|\\blabel\\s*=\\s*)" +
                "(\"[^\"]*\")",
        )
    }
}

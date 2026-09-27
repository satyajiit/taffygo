// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.CoreFailureCode
import taffy.core_api.TaskPhase

/**
 * The failed-task card's heading and sentence, and which reason each code is.
 *
 * On a phone, task `1bbba5cb` ended with one card reading "Taffy stopped
 * unexpectedly" as its heading and "Taffy stopped unexpectedly" as its
 * sentence. Two things were wrong at once. The internal reason carried the same
 * words under two resource names, so the card said one thing twice; and the
 * code that put the task there was `OUTCOME_UNKNOWN` — a link the browser had
 * refused on purpose, recorded as a step that may have happened — which is not
 * Taffy breaking at all (decision 0229).
 *
 * The heading and the sentence are compared as **words**, from the resource
 * files, in every locale. Comparing resource names would have passed on the
 * defect, because the two names were different and held the same sentence.
 */
class TaskFailureCopyTest {

    @Test
    fun `the task that showed one sentence twice now names an unsure step`() {
        // The shape the core published for 1bbba5cb: a task still held for the
        // person, one action whose outcome is unknown, and the phase that
        // outranks every live state while that is so.
        val held = task(
            phase = TaskPhase.OUTCOME_UNKNOWN,
            failure = CoreFailureCode.OUTCOME_UNKNOWN,
        ).copy(
            statusMessageKey = "task.outcome_unknown",
            allowedControls = listOf(TaskControl.TAKE_OVER, TaskControl.STOP),
        )

        val view = projectTaskView(ready(held))
        val bar = projectAssistantBar(ready(held))

        assertEquals(TaskDisplayState.FAILED, view.state)
        assertEquals(TaskFailureReason.OUTCOME_UNKNOWN, view.failure)
        assertEquals(TaskFailureReason.OUTCOME_UNKNOWN, bar.failure)
        assertEquals(
            R.string.taffy_task_failed_head_outcome_unknown,
            taskFailureHeadline(TaskFailureReason.OUTCOME_UNKNOWN),
        )
        assertEquals(
            R.string.taffy_assistant_failed_outcome_unknown,
            taskFailureLine(TaskFailureReason.OUTCOME_UNKNOWN),
        )
        assertNotEquals(
            text("values", R.string.taffy_task_failed_head_internal),
            text("values", taskFailureHeadline(TaskFailureReason.OUTCOME_UNKNOWN)),
        )
        assertNotEquals(
            text("values", R.string.taffy_task_failed_head_internal),
            text("values", taskFailureLine(TaskFailureReason.OUTCOME_UNKNOWN)),
        )
    }

    @Test
    fun `no reason's heading and sentence are the same words in any locale`() {
        for (locale in LOCALES) {
            for (reason in TaskFailureReason.entries) {
                val heading = text(locale, taskFailureHeadline(reason))
                val sentence = text(locale, taskFailureLine(reason))
                assertNotEquals("$locale $reason", heading, sentence)
            }
        }
    }

    @Test
    fun `no two reasons share a heading or a sentence`() {
        // A reason whose words are another's has not been given its own; that
        // is how an unsure step came to read as Taffy breaking.
        for (locale in LOCALES) {
            val headings = TaskFailureReason.entries.map { text(locale, taskFailureHeadline(it)) }
            val sentences = TaskFailureReason.entries.map { text(locale, taskFailureLine(it)) }
            assertEquals(locale, headings.size, headings.toSet().size)
            assertEquals(locale, sentences.size, sentences.toSet().size)
        }
    }

    @Test
    fun `only Taffy's own failures are called internal`() {
        val internal = CoreFailureCode.entries.filter {
            it.toTaskFailureReason() == TaskFailureReason.INTERNAL
        }.toSet()

        // The one the core reports on a task, and five it never puts on one.
        assertEquals(
            setOf(
                CoreFailureCode.JOURNAL_UNUSABLE,
                CoreFailureCode.INTERNAL,
                CoreFailureCode.CANCELLED,
                CoreFailureCode.CORE_UNAVAILABLE,
                CoreFailureCode.INVALID_REQUEST,
                CoreFailureCode.BACKPRESSURE,
            ),
            internal,
        )
        assertEquals(
            TaskFailureReason.OUTCOME_UNKNOWN,
            CoreFailureCode.OUTCOME_UNKNOWN.toTaskFailureReason(),
        )
        assertEquals(
            TaskFailureReason.DEADLINE_EXCEEDED,
            CoreFailureCode.DEADLINE_EXCEEDED.toTaskFailureReason(),
        )
    }

    @Test
    fun `the internal sentence says what to do and not only that Taffy stopped`() {
        val heading = text("values", taskFailureHeadline(TaskFailureReason.INTERNAL))
        val sentence = text("values", taskFailureLine(TaskFailureReason.INTERNAL))

        assertEquals("Taffy stopped unexpectedly", heading)
        assertTrue(sentence, "asking again" in sentence)
        // A failed task the core named no reason for reads the same sentence.
        assertEquals(taskFailureLine(TaskFailureReason.INTERNAL), taskFailureLine(null))
    }

    @Test
    fun `a notice's heading and body are never the same words either`() {
        for (locale in LOCALES) {
            for (notice in TaskNotice.entries) {
                val lines = listOf(noticeTitle(notice), noticeBody(notice), noticeChipLine(notice))
                    .map { text(locale, it) }
                assertEquals("$locale $notice", lines.size, lines.toSet().size)
            }
        }
    }

    @Test
    fun `the resource files this test reads are the ones the module ships`() {
        // The honesty half: an unreadable file would make every lookup above
        // fail loudly, but a file that parsed to nothing would not.
        for (locale in LOCALES) {
            assertTrue(locale, strings(locale).size > MINIMUM_STRINGS)
        }
    }

    private fun text(locale: String, id: Int): String {
        val name = STRING_NAMES[id] ?: error("no R.string field has the value $id")
        return strings(locale)[name] ?: error("$name is missing from $locale")
    }

    private fun strings(locale: String): Map<String, String> = catalogues.getOrPut(locale) {
        STRING.findAll(repositoryFile("$RES_PATH/$locale/strings.xml").readText())
            .associate { it.groupValues[1] to it.groupValues[2].trim() }
    }

    /**
     * One repository file, found by walking up from wherever the test runs,
     * the way `ProviderSignInFlowsParityTest` finds its inputs.
     */
    private fun repositoryFile(relativePath: String): File {
        var directory: File? = File(System.getProperty("user.dir").orEmpty()).absoluteFile
        while (directory != null) {
            if (File(directory, ROOT_MARKER).isFile) {
                val found = File(directory, relativePath)
                check(found.isFile) { "$relativePath is missing from the repository" }
                return found
            }
            directory = directory.parentFile
        }
        error("no repository root above ${System.getProperty("user.dir")}")
    }

    private fun ready(task: TaskProjection?) =
        TaskRepositoryState(CoreUiAvailability.READY, 1u, task)

    private fun task(phase: TaskPhase, failure: CoreFailureCode?) = TaskProjection(
        id = "task-1bbba5cb",
        revision = 9u,
        phase = phase,
        goal = "download my eAadhaar",
        template = TaskTemplate.WEB_ERRAND,
        progressBasisPoints = 0u,
        statusMessageKey = null,
        failure = failure,
        pendingAction = null,
    )

    private val catalogues = mutableMapOf<String, Map<String, String>>()

    private companion object {
        const val ROOT_MARKER = "taffy-core/build/components.toml"
        const val RES_PATH = "taffy-core/ui/android/feature/assistant/src/main/res"
        const val MINIMUM_STRINGS = 50
        val LOCALES = listOf("values", "values-hi")
        val STRING = Regex("""<string name="([a-z0-9_]+)"[^>]*>(.*?)</string>""", RegexOption.DOT_MATCHES_ALL)

        /** Resource id to name, read off the generated class the ids live on. */
        val STRING_NAMES: Map<Int, String> = R.string::class.java.fields
            .filter { it.type == Int::class.javaPrimitiveType }
            .associate { it.getInt(null) to it.name }
    }
}

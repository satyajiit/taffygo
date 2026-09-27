// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskProjection
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import taffy.core_api.TaskPhase

@OptIn(ExperimentalCoroutinesApi::class)
class TaskDownloadsViewModelTest {
    private val dispatcher = StandardTestDispatcher()
    private val browser = TaskBrowserTestRepository()
    private val tasks = TestTasks()
    private val file = DownloadRecord(DownloadId("owned-file"), "document.pdf", "identity.example.test",
        1024L, 1024L, DownloadState.COMPLETE, setOf(DownloadAction.OPEN), "application/pdf")

    @Before fun setUp() {
        Dispatchers.setMain(dispatcher)
        publish(TaskPhase.COMPLETED)
    }
    @After fun tearDown() = Dispatchers.resetMain()

    @Test fun `task completion alone is not a download and new browser publication refreshes files`() = runTest(dispatcher) {
        val model = TaskDownloadsViewModel(browser, tasks)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        assertTrue(model.state.value.files.isEmpty())
        browser.completedFiles = { listOf(file, file.copy(id = DownloadId("unfinished"), state = DownloadState.RUNNING),
            file.copy(id = DownloadId("unopenable"), allowedActions = emptySet())) }
        browser.downloads.value = listOf(file)
        runCurrent()
        assertEquals(listOf(file), model.state.value.files)
        assertEquals(listOf("finished", "finished"), browser.fileQueries)
        assertTrue(browser.fileOpens.isEmpty())
    }

    @Test fun `an explicit open names exact task and file and waits for fresh browser admission`() = runTest(dispatcher) {
        browser.completedFiles = { listOf(file) }
        val answer = CompletableDeferred<Boolean>()
        browser.openFile = { _, _ -> answer.await() }
        val model = TaskDownloadsViewModel(browser, tasks)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        model.open("finished", file.id)
        model.open("finished", file.id)
        runCurrent()
        assertEquals(listOf("finished" to file.id), browser.fileOpens)
        assertEquals(file.id, model.state.value.opening)
        assertNull(model.state.value.failed)
        answer.complete(false)
        runCurrent()
        assertNull(model.state.value.opening)
        assertEquals(file.id, model.state.value.failed)
    }

    @Test fun `a previous tasks delayed files and stale click cannot enter the next task`() = runTest(dispatcher) {
        val answer = CompletableDeferred<List<DownloadRecord>>()
        browser.completedFiles = { if (it == "finished") answer.await() else emptyList() }
        val model = TaskDownloadsViewModel(browser, tasks)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        publish(TaskPhase.COMPLETED, id = "next")
        runCurrent()
        answer.complete(listOf(file))
        runCurrent()
        model.open("finished", file.id)
        runCurrent()
        assertEquals("next", model.state.value.taskId)
        assertTrue(model.state.value.files.isEmpty())
        assertTrue(browser.fileOpens.isEmpty())
    }

    @Test fun `only completed partial and failed tasks can offer their actual completed files`() = runTest(dispatcher) {
        browser.completedFiles = { listOf(file) }
        val model = TaskDownloadsViewModel(browser, tasks)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        for (phase in listOf(TaskPhase.COMPLETED, TaskPhase.PARTIAL, TaskPhase.FAILED)) {
            publish(phase)
            runCurrent()
            assertEquals(listOf(file), model.state.value.files)
        }
        publish(TaskPhase.RUNNING)
        runCurrent()
        assertTrue(model.state.value.files.isEmpty())
        model.open("finished", file.id)
        runCurrent()
        assertTrue(browser.fileOpens.isEmpty())
    }

    private fun publish(phase: TaskPhase, id: String = "finished") {
        tasks.status.value = tasks.status.value.copy(task = TaskProjection(
            id = id, revision = 1uL, phase = phase, goal = "Download my document",
            template = TaskTemplate.WEB_ERRAND, progressBasisPoints = 10_000u,
            statusMessageKey = null, failure = null, pendingAction = null,
        ))
    }
}

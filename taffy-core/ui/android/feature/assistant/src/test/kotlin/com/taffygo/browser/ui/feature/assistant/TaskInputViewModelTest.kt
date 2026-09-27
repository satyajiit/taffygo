// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.TaskChallengeKind
import com.taffygo.browser.ui.core.model.TaskInputField
import com.taffygo.browser.ui.core.model.TaskInputRequest
import com.taffygo.browser.ui.core.task.TaskInputRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * Where a typed value goes, and what happens to it when it does not go
 * anywhere.
 *
 * The three claims worth a test: what is typed reaches the vault seam and only
 * the vault seam; putting the sheet away keeps it and keeps the request; and a
 * request that is answered leaves nothing behind.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class TaskInputViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `a described form opens with a row per field`() = runTest(dispatcher) {
        val forms = FakeForms(request())
        val viewModel = TaskInputViewModel(forms)
        runCurrent()

        assertTrue(viewModel.state.value.open)
        assertEquals(listOf("reference", "code"), viewModel.state.value.rows.map { it.id })
    }

    @Test
    fun `no vault behind the seam is no form, whatever the browser describes`() =
        runTest(dispatcher) {
            val forms = FakeForms(request(), isAvailable = false)
            val viewModel = TaskInputViewModel(forms)
            runCurrent()

            assertFalse(viewModel.state.value.hasRequest)
        }

    @Test
    fun `what is typed goes to the vault seam, keyed by field, and nowhere else`() =
        runTest(dispatcher) {
            val forms = FakeForms(request())
            val viewModel = TaskInputViewModel(forms)
            runCurrent()

            viewModel.onIntent(TaskInputIntent.ValueChanged("reference", "AB-1234"))
            viewModel.onIntent(TaskInputIntent.ValueChanged("code", "927451"))
            viewModel.onIntent(TaskInputIntent.Submit)
            runCurrent()

            // The first control only exposes the exact review. Values have
            // not crossed the browser seam until the separate approval.
            assertNull(forms.submitted)
            assertTrue(viewModel.state.value.reviewing)

            viewModel.onIntent(TaskInputIntent.Confirm)
            runCurrent()

            assertEquals("req-1", forms.submittedRequestId)
            assertEquals(mapOf("reference" to "AB-1234", "code" to "927451"), forms.submitted)
        }

    @Test
    fun `an answered request leaves nothing behind`() = runTest(dispatcher) {
        val forms = FakeForms(request())
        val viewModel = TaskInputViewModel(forms)
        runCurrent()

        viewModel.onIntent(TaskInputIntent.ValueChanged("reference", "AB-1234"))
        viewModel.onIntent(TaskInputIntent.ValueChanged("code", "927451"))
        viewModel.onIntent(TaskInputIntent.Submit)
        viewModel.onIntent(TaskInputIntent.Confirm)
        runCurrent()

        val after = viewModel.state.value
        assertFalse(after.hasRequest)
        assertTrue(after.rows.isEmpty())
        assertFalse(after.toString().contains("927451"))
    }

    @Test
    fun `a refusal keeps what was typed so a person does not retype it`() =
        runTest(dispatcher) {
            val forms = FakeForms(request(), answer = TaffyResult.Failure(FailureReason.BACKPRESSURE))
            val viewModel = TaskInputViewModel(forms)
            runCurrent()

            viewModel.onIntent(TaskInputIntent.ValueChanged("reference", "AB-1234"))
            viewModel.onIntent(TaskInputIntent.ValueChanged("code", "927451"))
            viewModel.onIntent(TaskInputIntent.Submit)
            viewModel.onIntent(TaskInputIntent.Confirm)
            runCurrent()

            val after = viewModel.state.value
            assertEquals(FailureReason.BACKPRESSURE, after.failure)
            assertFalse(after.submitting)
            assertEquals("AB-1234", after.rows.first { it.id == "reference" }.value)
        }

    @Test
    fun `putting the sheet away answers nothing and keeps the request open`() =
        runTest(dispatcher) {
            val forms = FakeForms(request())
            val viewModel = TaskInputViewModel(forms)
            runCurrent()

            viewModel.onIntent(TaskInputIntent.ValueChanged("reference", "AB-1234"))
            viewModel.onIntent(TaskInputIntent.Dismiss)
            runCurrent()

            assertNull(forms.submitted)
            assertNull(forms.completed)
            assertFalse(viewModel.state.value.open)
            assertTrue(viewModel.state.value.hasRequest)

            viewModel.onIntent(TaskInputIntent.Reopen)
            assertTrue(viewModel.state.value.open)
            assertEquals("AB-1234", viewModel.state.value.rows.first().value)
        }

    @Test
    fun `a widget on the page is finished by saying so, with nothing sent`() =
        runTest(dispatcher) {
            val interactive = TaskInputRequest(
                requestId = "req-9",
                host = "portal.example.test",
                fields = listOf(
                    TaskInputField(
                        id = "widget",
                        label = "Confirm you are a person",
                        challenge = TaskChallengeKind.INTERACTIVE_CHALLENGE,
                    ),
                ),
            )
            val forms = FakeForms(interactive)
            val viewModel = TaskInputViewModel(forms)
            runCurrent()

            viewModel.onIntent(TaskInputIntent.CompleteInteractive)
            runCurrent()

            assertEquals("req-9", forms.completed)
            assertNull(forms.submitted)
        }

    private fun request() = TaskInputRequest(
        requestId = "req-1",
        host = "portal.example.test",
        fields = listOf(
            TaskInputField(id = "reference", label = "Reference"),
            TaskInputField(
                id = "code",
                label = "Code",
                sensitive = true,
                challenge = TaskChallengeKind.ONE_TIME_CODE,
            ),
        ),
    )

    /** A vault seam that records what it was handed and answers as told. */
    private class FakeForms(
        open: TaskInputRequest?,
        override val isAvailable: Boolean = true,
        private val answer: TaffyResult<Unit> = TaffyResult.Success(Unit),
    ) : TaskInputRepository {
        var submitted: Map<String, String>? = null
            private set
        var submittedRequestId: String? = null
            private set
        var completed: String? = null
            private set

        override val request: StateFlow<TaskInputRequest?> = MutableStateFlow(open)

        override suspend fun submit(
            requestId: String,
            values: Map<String, String>,
        ): TaffyResult<Unit> {
            submittedRequestId = requestId
            submitted = values
            return answer
        }

        override suspend fun completeInteractive(requestId: String): TaffyResult<Unit> {
            completed = requestId
            return answer
        }
    }
}

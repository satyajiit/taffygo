// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import java.io.Closeable
import com.taffygo.browser.ui.core.common.AppDispatchers
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumPageZoomRepositoryTest {

    @Test
    fun state_tracks_the_selected_page_and_native_change_signal() {
        val page = FakePage(zoomFactor = 1.22, defaultZoomFactor = 0.0)
        val environment = FakeEnvironment(page)
        val repository = ChromiumPageZoomRepository(environment, testDispatchers())

        assertEquals(125, repository.state.value.percent)
        assertTrue(repository.state.value.available)
        assertTrue(repository.state.value.canReset)

        page.zoomFactor = 0.0
        environment.publish()
        assertEquals(100, repository.state.value.percent)
        assertFalse(repository.state.value.canReset)
    }

    @Test
    fun every_action_re_resolves_the_selected_page() = runBlocking {
        val first = FakePage()
        val second = FakePage()
        val environment = FakeEnvironment(first)
        val repository = ChromiumPageZoomRepository(environment, testDispatchers())

        environment.page = second
        repository.zoomIn()
        repository.zoomOut()
        repository.reset()

        assertEquals(0, first.actions)
        assertEquals(3, second.actions)
    }

    @Test
    fun invalid_factors_and_destroyed_repository_fail_closed() {
        val environment = FakeEnvironment(FakePage(zoomFactor = Double.NaN))
        val repository = ChromiumPageZoomRepository(environment, testDispatchers())
        assertFalse(repository.state.value.available)

        repository.destroy()
        assertTrue(environment.observationClosed)
        assertFalse(repository.state.value.available)
        environment.page = FakePage(zoomFactor = 1.22)
        environment.publish()
        assertFalse(repository.state.value.available)
    }

    private fun testDispatchers(): AppDispatchers = object : AppDispatchers {
        override val main = Dispatchers.Unconfined
        override val io = Dispatchers.Unconfined
        override val default = Dispatchers.Unconfined
    }

    private class FakeEnvironment(
        var page: FakePage?,
    ) : ChromiumPageZoomRepository.Environment {
        private var observer: (() -> Unit)? = null
        var observationClosed = false

        override fun selectedPage(): ChromiumPageZoomRepository.Page? = page

        override fun observe(onChanged: () -> Unit): Closeable {
            observer = onChanged
            return Closeable {
                observationClosed = true
                observer = null
            }
        }

        fun publish() = observer?.invoke() ?: Unit
    }

    private class FakePage(
        override var zoomFactor: Double = 0.0,
        override var defaultZoomFactor: Double = 0.0,
    ) : ChromiumPageZoomRepository.Page {
        var actions = 0
        override fun zoomIn() {
            actions += 1
        }

        override fun zoomOut() {
            actions += 1
        }

        override fun reset() {
            actions += 1
        }
    }
}

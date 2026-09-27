// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Empty is Ready with no visits, never a sample trail. */
class EmptyHistoryRepositoryTest {

    @Test
    fun `empty is ready and invents no visits`() = runTest {
        val repository = EmptyHistoryRepository()
        val ready = repository.snapshot.value as HistorySnapshot.Ready

        assertTrue(ready.visits.isEmpty())
        repository.delete(HistoryVisit.Id("hv_1"))
        assertEquals(emptyList<HistoryVisit>(), (repository.snapshot.value as HistorySnapshot.Ready).visits)
    }

    @Test
    fun `unavailable is not empty`() {
        assertEquals(
            HistorySnapshot.Unavailable,
            UnavailableHistoryRepository().snapshot.value,
        )
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.LocalProfile
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId

/** Sample ports and states for You-hub unit tests. Never a live secret. */
internal object YouFixtures {

    /** A phone whose owner has set a name and kept the monogram. */
    val named = LocalProfile(displayName = "Priya Sharma")

    val timeUnavailable = TimeOnSitesRepository.Snapshot(
        availability = YouSurfaceAvailability.UNAVAILABLE,
    )

    val timeReady = TimeOnSitesRepository.Snapshot(
        availability = YouSurfaceAvailability.READY,
        today = listOf(
            TimeOnSitesRepository.Site("youtube.com", 48L * 60_000),
            TimeOnSitesRepository.Site("croma.com", 22L * 60_000),
        ),
        week = listOf(
            TimeOnSitesRepository.Site("youtube.com", 3L * 60L * 60_000),
            TimeOnSitesRepository.Site("wikipedia.org", 11L * 60_000),
        ),
    )

    val memoryEmpty = MemoryRepository.Snapshot(
        availability = YouSurfaceAvailability.READY,
        processScoped = true,
    )

    val memoryNotes = listOf(
        MemoryRepository.Note(
            id = "m1",
            statement = "Prefers window seats",
            source = MemoryRepository.Source.YOU_WROTE,
            addedEpochDay = 20_300,
        ),
        MemoryRepository.Note(
            id = "m2",
            statement = "Answers in short bullet points",
            source = MemoryRepository.Source.TAFFY_NOTICED,
            addedEpochDay = 20_301,
            workspaceName = "TV",
        ),
    )

    val signInsUnavailable = SavedSignInsRepository.Snapshot(
        availability = YouSurfaceAvailability.UNAVAILABLE,
    )

    val signInRecord = SavedSignInsRepository.Record(
        id = "s1",
        site = "croma.com",
        username = "you@email.example",
        lastUsedEpochMillis = 1_780_000_000_000L,
    )

    val detailsEmpty = SavedDetailsRepository.Snapshot(
        availability = YouSurfaceAvailability.READY,
    )

    val person = SavedDetailsRepository.Person(
        id = "p1",
        givenName = "Priya",
        familyName = "Sharma",
        email = "priya@email.example",
        phone = "98765 43210",
    )

    fun tab(private: Boolean = false) = Tab(
        id = TabId("tab"),
        title = "Page",
        host = "example.test",
        isPrivate = private,
        isSelected = true,
    )
}

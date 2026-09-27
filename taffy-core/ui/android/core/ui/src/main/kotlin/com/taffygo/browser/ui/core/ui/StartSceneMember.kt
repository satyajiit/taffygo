// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * Which painted plate the start page draws, and where it lives in the pack.
 *
 * The choice is a pure function of the device's own clock and nothing else. No
 * randomness, so a host test can state the answer; no persisted preference, so
 * there is nothing to migrate; and nothing leaves the device, because the only
 * input is what time it is here.
 *
 * A part of the day gets [VARIANTS_PER_PART] plates rather than one, so the
 * page is not the same picture every morning for the life of the install. The
 * variant is taken from the day of the year, which makes it stable for the
 * whole of one part of one day — a plate that changed while a person was
 * looking at it would read as a fault — and different tomorrow. The part is
 * added in so the four parts of one day do not all land on the same index.
 *
 * The member path is built here rather than by the caller, and refuses a
 * variant the pack has no member for, because a read that cannot succeed is
 * worth refusing before it reaches the delivery plane.
 */
object StartSceneMember {

    /** The catalog identity of the pack these plates arrive in. */
    const val ASSET_ID: String = "start-scenes"

    /** How many plates each part of the day has. */
    const val VARIANTS_PER_PART: Int = 6

    /**
     * The plate for a local hour and day of the year.
     *
     * [hourOfDay] outside 0..23 falls to night rather than throwing: this is
     * decoration, and a clock the platform reported oddly should cost a person
     * a picture rather than a screen.
     */
    fun sceneFor(hourOfDay: Int, dayOfYear: Int): TaffyStartScene {
        val part = partFor(hourOfDay)
        // `floorMod` rather than `%`: a negative day of the year is not a day
        // this can be asked about, but an index of -2 would be, and it would
        // be an index no member answers to.
        val variant = Math.floorMod(dayOfYear + part.ordinal, VARIANTS_PER_PART)
        return TaffyStartScene(part, variant)
    }

    /** Where the day is divided; [TaffyDayPart] says why it is divided there. */
    fun partFor(hourOfDay: Int): TaffyDayPart = when (hourOfDay) {
        in 5..11 -> TaffyDayPart.MORNING
        in 12..16 -> TaffyDayPart.AFTERNOON
        in 17..20 -> TaffyDayPart.EVENING
        else -> TaffyDayPart.NIGHT
    }

    /** The pack member for one plate, or null when the pack has no such member. */
    fun pathFor(scene: TaffyStartScene): String? {
        if (scene.variant !in 0 until VARIANTS_PER_PART) return null
        return "scenes/${scene.part.member}-${scene.variant + 1}.webp"
    }

    /** Every member the pack is expected to carry, in a stable order. */
    fun allMembers(): List<String> = TaffyDayPart.entries.flatMap { part ->
        (0 until VARIANTS_PER_PART).map { variant ->
            "scenes/${part.member}-${variant + 1}.webp"
        }
    }
}

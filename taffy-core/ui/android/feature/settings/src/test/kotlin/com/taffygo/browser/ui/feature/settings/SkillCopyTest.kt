// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.ui.TaffyIcon
import org.junit.Assert.assertEquals
import org.junit.Test

/** Each built-in ability draws its own outline mark. */
class SkillCopyTest {

    @Test
    fun everyBuiltInSkillHasItsOwnOutlineIcon() {
        val icons = SkillsRepository.previewBuiltIns().map { skillIcon(it.id) }
        assertEquals(icons.size, icons.toSet().size)
    }

    @Test
    fun anUnknownSkillFallsBackToThePuzzleMark() {
        assertEquals(TaffyIcon.PuzzlePiece, skillIcon("not-installed"))
    }
}

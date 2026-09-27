// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.ui.graphics.vector.VectorPath
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The vendored provider marks parse, and each carries exactly as many paths as
 * the source file it was transcribed from.
 *
 * Transcribed path data fails at the moment it is drawn, which on a vendored
 * asset means on a device, in one provider row of one screen. Parsing every
 * mark here turns "it should render" into something a host command prints.
 *
 * The path count is asserted per mark rather than fixed at one, because two
 * sources are not one path: Cerebras ships the rings and the letter separately
 * and Together ships three petals. A mark that quietly lost or gained a path in
 * transcription is a different outline from the one the provenance record
 * checksums, so the number is written down beside it.
 *
 * The list is hand-maintained, so a mark added to [ProviderMark] and not added
 * here is simply untested. That is the one failure this file cannot catch by
 * itself; `core/ui/vendor/provider-marks.txt` names the set a reviewer checks
 * it against.
 */
class ProviderMarkTest {

    /** Each mark, with the number of paths its source file carries. */
    private val marks = listOf(
        ProviderMark.Anthropic to 1,
        ProviderMark.Baseten to 1,
        ProviderMark.Cerebras to 2,
        ProviderMark.DeepSeek to 1,
        ProviderMark.Fireworks to 1,
        ProviderMark.GitHubCopilot to 1,
        ProviderMark.GoogleGemini to 1,
        ProviderMark.Groq to 1,
        ProviderMark.Kimi to 1,
        ProviderMark.MiniMax to 1,
        ProviderMark.MoonshotAi to 1,
        ProviderMark.OpenAi to 1,
        ProviderMark.OpenRouter to 1,
        ProviderMark.Together to 3,
        ProviderMark.Xai to 1,
    )

    @Test
    fun `every mark parses into the paths its source file carries`() {
        marks.forEach { (mark, expected) ->
            val paths = mark.root.filterIsInstance<VectorPath>()
            assertEquals(mark.name, expected, paths.size)
            paths.forEach { path -> assertTrue(mark.name, path.pathData.isNotEmpty()) }
        }
    }

    @Test
    fun `every mark keeps the source viewport, so none is silently rescaled`() {
        marks.forEach { (mark, _) ->
            assertEquals(mark.name, 24f, mark.viewportWidth, 0f)
            assertEquals(mark.name, 24f, mark.viewportHeight, 0f)
        }
    }

    @Test
    fun `every mark names itself, so a wrong glyph is readable in a dump`() {
        val names = marks.map { (mark, _) -> mark.name }

        assertEquals(names.distinct(), names)
        assertTrue(names.toString(), names.all { it.startsWith("ProviderMark.") })
    }

    @Test
    fun `every mark draws inside its own viewport`() {
        marks.forEach { (mark, _) ->
            assertEquals(mark.name, mark.viewportWidth, mark.defaultWidth.value, 0f)
            assertEquals(mark.name, mark.viewportHeight, mark.defaultHeight.value, 0f)
        }
    }
}

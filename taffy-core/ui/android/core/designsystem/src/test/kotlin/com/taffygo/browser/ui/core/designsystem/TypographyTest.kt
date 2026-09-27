// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import com.taffygo.browser.ui.core.designsystem.internal.TaffyTypeScale
import com.taffygo.browser.ui.core.designsystem.internal.taffyMaterialTypography
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Holds the compact semantic hierarchy and its accessibility properties. */
class TypographyTest {

    @Test
    fun `phone reading roles follow the compact semantic recipe`() {
        val scale = TaffyTypeScale.scale
        val expected = mapOf(
            "body" to 14f,
            "detail" to 13f,
            "caption" to 12f,
            "micro" to 11f,
        )

        for ((name, size) in expected) {
            val actual = roles(scale).first { it.first == name }.second.fontSize.value
            assertEquals(name, size, actual, 0f)
        }
    }

    @Test
    fun `the body and smallest text floors are explicit`() {
        assertEquals(14, TaffyTypeScale.MINIMUM_BODY_SIZE_SP)
        assertEquals(11, TaffyTypeScale.MINIMUM_TEXT_SIZE_SP)
        for ((scaleName, scale) in scales()) {
            assertTrue(
                "$scaleName body is ${scale.body.fontSize}",
                scale.body.fontSize.value >= TaffyTypeScale.MINIMUM_BODY_SIZE_SP,
            )
            for ((name, style) in roles(scale)) {
                assertTrue(
                    "$scaleName $name is ${style.fontSize}",
                    style.fontSize.value >= TaffyTypeScale.MINIMUM_TEXT_SIZE_SP,
                )
            }
        }
    }

    @Test
    fun `supporting copy steps down once per semantic role`() {
        for ((scaleName, scale) in scales()) {
            assertTrue(scaleName, scale.title.fontSize.value > scale.body.fontSize.value)
            assertTrue(scaleName, scale.body.fontSize.value > scale.detail.fontSize.value)
            assertTrue(scaleName, scale.detail.fontSize.value > scale.caption.fontSize.value)
            assertTrue(scaleName, scale.caption.fontSize.value > scale.micro.fontSize.value)
            assertEquals(scaleName, scale.detail.fontSize.value, scale.label.fontSize.value, 0f)
            assertEquals(scaleName, scale.body.fontSize.value, scale.numeric.fontSize.value, 0f)
        }
    }

    @Test
    fun `semantic roles carry the intended emphasis`() {
        for ((scaleName, scale) in scales()) {
            assertEquals(scaleName, FontWeight.W400.weight, weightOf(scale.body))
            assertEquals(scaleName, FontWeight.W400.weight, weightOf(scale.detail))
            assertEquals(scaleName, FontWeight.W500.weight, weightOf(scale.caption))
            assertEquals(scaleName, FontWeight.W500.weight, weightOf(scale.micro))
            assertEquals(scaleName, FontWeight.W700.weight, weightOf(scale.label))
            assertEquals(scaleName, FontWeight.W700.weight, weightOf(scale.title))
        }
    }

    @Test
    fun `leading remains readable in every role`() {
        for ((scaleName, scale) in scales()) {
            for ((name, style) in roles(scale)) {
                val leading = leadingOf(style)
                assertTrue("$scaleName $name leads at $leading", leading in 1.20f..1.50f)
            }
            assertTrue(scaleName, leadingOf(scale.body) >= 1.38f)
            assertTrue(scaleName, leadingOf(scale.detail) >= 1.38f)
        }
    }

    @Test
    fun `only display copy tightens its tracking`() {
        for ((scaleName, scale) in scales()) {
            assertTrue(scaleName, scale.display.letterSpacing.value < 0f)
            for ((name, style) in roles(scale).filter { it.first != "display" }) {
                assertEquals("$scaleName $name", 0f, style.letterSpacing.value, 0f)
            }
        }
    }

    @Test
    fun `every metric follows the device font scale`() {
        for ((scaleName, scale) in scales()) {
            for ((name, style) in roles(scale)) {
                assertTrue("$scaleName $name size", style.fontSize.isSp)
                assertTrue("$scaleName $name line height", style.lineHeight.isSp)
                assertTrue("$scaleName $name tracking", style.letterSpacing.isSp)
            }
        }
    }

    @Test
    fun `every role uses the vendored product typeface`() {
        for ((scaleName, scale) in scales()) {
            for ((name, style) in roles(scale)) {
                assertEquals("$scaleName $name", TaffyTypeScale.PRODUCT_TYPEFACE, style.fontFamily)
            }
        }
        assertNotEquals(FontFamily.SansSerif, TaffyTypeScale.PRODUCT_TYPEFACE)
        assertNotEquals(FontFamily.Default, TaffyTypeScale.PRODUCT_TYPEFACE)
    }

    @Test
    fun `numbers that update in place use tabular figures`() {
        for ((scaleName, scale) in scales()) {
            assertTrue(scaleName, scale.numeric.fontFeatureSettings.orEmpty().contains("tnum"))
        }
    }

    @Test
    fun `the Material bridge preserves supporting hierarchy`() {
        val scale = TaffyTypeScale.scale
        val material = taffyMaterialTypography(scale)

        assertEquals(scale.body, material.bodyLarge)
        assertEquals(scale.detail, material.bodyMedium)
        assertEquals(scale.caption, material.bodySmall)
        assertEquals(scale.label, material.labelLarge)
        assertEquals(scale.caption, material.labelMedium)
        assertEquals(scale.micro, material.labelSmall)
    }

    @Test
    fun `tablet steps headings up but preserves compact reading roles`() {
        val phone = TaffyTypeScale.scale
        val tablet = TaffyTypeScale.tabletScale

        assertTrue(tablet.display.fontSize.value > phone.display.fontSize.value)
        assertTrue(tablet.headline.fontSize.value > phone.headline.fontSize.value)
        assertTrue(tablet.title.fontSize.value > phone.title.fontSize.value)
        for (role in listOf("body", "detail", "caption", "micro", "label", "numeric")) {
            val phoneSize = roles(phone).first { it.first == role }.second.fontSize.value
            val tabletSize = roles(tablet).first { it.first == role }.second.fontSize.value
            assertEquals(role, phoneSize, tabletSize, 0f)
        }
    }

    private fun leadingOf(style: TextStyle) = style.lineHeight.value / style.fontSize.value

    private fun weightOf(style: TextStyle) = style.fontWeight?.weight ?: 0

    private fun scales() = listOf(
        "phone" to TaffyTypeScale.scale,
        "tablet" to TaffyTypeScale.tabletScale,
    )

    private fun roles(scale: TaffyTypography) = with(scale) {
        listOf(
            "display" to display,
            "headline" to headline,
            "title" to title,
            "body" to body,
            "detail" to detail,
            "caption" to caption,
            "micro" to micro,
            "label" to label,
            "numeric" to numeric,
        )
    }
}

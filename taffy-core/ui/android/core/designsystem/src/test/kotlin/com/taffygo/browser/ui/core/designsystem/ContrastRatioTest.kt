// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import com.taffygo.browser.ui.core.designsystem.internal.ContrastRatio
import com.taffygo.browser.ui.core.designsystem.internal.PaletteValues
import com.taffygo.browser.ui.core.designsystem.internal.TaffyPalette
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Parity row PAR-A11Y-004, as a check rather than a promise: the handoff's
 * contrast rule, "every text token clears 4.5:1 on the surface it is named
 * for" (`handoff/DESIGN.md` section 2.6), encoded per surface pairing.
 *
 * 4.5:1 is a floor, and the dark theme is deliberately held above it. A floor
 * is the point at which text stops being a violation, not the point at which
 * it becomes comfortable, and the dark palette used to sit on it: supporting
 * text measured 4.51:1 on a card, cleared the assertion, and was reported as
 * unreadable on an OLED phone. The dark bounds below record where the
 * re-derived palette actually lands, so the distance between conforming and
 * readable cannot be given back by a later edit without this file failing.
 *
 * This is still deliberately not a blanket every-token-on-every-surface rule:
 * the light scale runs darker than the dark one on purpose, `accentText` only
 * exists for dark surfaces and `accentDeep` only for light ones, and
 * `hairline`/`outline` are strokes that never carry text.
 *
 * The palette values are `[Open (OD-071)]`. This test is what any resolution of
 * OD-071 has to keep passing.
 */
class ContrastRatioTest {

    @Test
    fun `textPrimary is legible on every surface in both themes`() {
        for ((themeName, palette) in themes()) {
            for ((background, backgroundValue) in backgrounds(palette)) {
                val ratio = ContrastRatio.between(palette.textPrimary, backgroundValue)
                // Heading text is held far above the floor in both themes,
                // and the ground — the surface most of a screen is — highest
                // of all. Light already measured this (16.97 / 15.16 / 17.98 /
                // 17.71) before the dark theme was re-derived to match.
                val bound = if (background == "surface") PRIMARY_ON_GROUND else PRIMARY_ON_LAYER
                assertTrue(
                    "$themeName: textPrimary on $background is $ratio",
                    ratio >= bound,
                )
            }
        }
    }

    @Test
    fun `textSecondary is legible on every surface in both themes`() {
        for ((themeName, palette) in themes()) {
            for ((background, value) in backgrounds(palette)) {
                val ratio = ContrastRatio.between(palette.textSecondary, value)
                // Dark carries the comfort bound; light stays at the floor,
                // because light textSecondary measures 5.46:1 on `surface` and
                // is not being changed — the light theme is the one that was
                // reported as good.
                val bound = if (themeName == "dark") SUPPORTING_COMFORT else ContrastRatio.TEXT_MINIMUM
                assertTrue(
                    "$themeName: textSecondary on $background is $ratio",
                    ratio >= bound,
                )
            }
        }
    }

    @Test
    fun `textTertiary is legible on the surfaces each theme names for it`() {
        for ((background, value) in backgrounds(TaffyPalette.dark)) {
            val ratio = ContrastRatio.between(TaffyPalette.dark.textTertiary, value)
            assertTrue("dark: textTertiary on $background is $ratio", ratio >= TERTIARY_COMFORT)
        }

        // Light is asserted on three of the four. It measures 4.63 on surface,
        // 4.90 on surfaceRaised and 4.83 on surfaceSheet, but 4.13 on
        // surfaceSunken: closing that would mean darkening tertiary past
        // secondary and flattening the paper tone ladder, which is a worse
        // defect than the one it would fix. Excluded on purpose, not missed.
        val light = TaffyPalette.light
        for ((background, value) in listOf(
            "surface" to light.surface,
            "surfaceRaised" to light.surfaceRaised,
            "surfaceSheet" to light.surfaceSheet,
        )) {
            val ratio = ContrastRatio.between(light.textTertiary, value)
            assertTrue("light: textTertiary on $background is $ratio", ratio >= ContrastRatio.TEXT_MINIMUM)
        }
    }

    @Test
    fun `dark supporting text is comfortable, not merely conforming, on every dark surface`() {
        val palette = TaffyPalette.dark
        for ((name, text, bound) in listOf(
            Triple("textSecondary", palette.textSecondary, SUPPORTING_COMFORT),
            Triple("textTertiary", palette.textTertiary, TERTIARY_COMFORT),
        )) {
            for ((background, value) in backgrounds(palette)) {
                val ratio = ContrastRatio.between(text, value)
                assertTrue(
                    "dark: $name on $background is $ratio",
                    ratio >= bound,
                )
            }
        }
    }

    @Test
    fun `accent-coloured text is legible on the surfaces the handoff names for it`() {
        val lightOnSurface = ContrastRatio.between(TaffyPalette.light.accentDeep, TaffyPalette.light.surface)
        assertTrue("light: accentDeep on surface is $lightOnSurface", lightOnSurface >= ContrastRatio.TEXT_MINIMUM)

        // Dark accentText is asserted on all four surfaces rather than two:
        // it measures 12.57 / 11.14 / 9.91 / 8.53, so there is no dark surface
        // it does not carry.
        for ((background, value) in backgrounds(TaffyPalette.dark)) {
            val ratio = ContrastRatio.between(TaffyPalette.dark.accentText, value)
            assertTrue("dark: accentText on $background is $ratio", ratio >= ContrastRatio.TEXT_MINIMUM)
        }
        // The crossed pairs fail by design: accentText is a light-on-dark step
        // (1.46:1 on paper) and accentDeep a dark-on-light one (3.45:1 on the
        // dark ground). Neither is asserted on the theme it does not belong to.
    }

    @Test
    fun `text on the accent is legible in both themes`() {
        for ((themeName, palette) in themes()) {
            val ratio = ContrastRatio.between(palette.accentOn, palette.accent)
            assertTrue("$themeName: accentOn on accent is $ratio", ratio >= ContrastRatio.TEXT_MINIMUM)
        }
    }

    @Test
    fun `danger and positive text are legible on every surface and on their own washes`() {
        for ((themeName, palette) in themes()) {
            val pairings = listOf(
                Triple("dangerText", palette.dangerText, palette.dangerWash),
                Triple("positiveText", palette.positiveText, palette.positiveWash),
            )
            for ((name, text, wash) in pairings) {
                for ((background, value) in backgrounds(palette)) {
                    val onSurface = ContrastRatio.between(text, value)
                    assertTrue(
                        "$themeName: $name on $background is $onSurface",
                        onSurface >= ContrastRatio.TEXT_MINIMUM,
                    )

                    // The pairing the handoff actually draws: text on the wash
                    // composited over the surface. In the light theme the wash
                    // lightens toward the text hue, so this is the harder case.
                    // Both themes clear it on all four surfaces; the worst
                    // measured pair is dark positiveText on its wash over
                    // surfaceSheet, at 5.94.
                    val onWash = ContrastRatio.between(text, compositeOver(wash, value))
                    assertTrue(
                        "$themeName: $name on its wash over $background is $onWash",
                        onWash >= ContrastRatio.TEXT_MINIMUM,
                    )
                }
            }
        }
    }

    @Test
    fun `strokes read as strokes on the surface in both themes`() {
        for ((themeName, palette) in themes()) {
            val outline = ContrastRatio.between(palette.outline, palette.surface)
            val hairline = ContrastRatio.between(palette.hairline, palette.surface)

            // The handoff's outline and hairline are tonal steps, not 3:1
            // graphic objects: measured 1.213 / 2.768 on light, 1.681 / 3.726
            // on dark. What the palette must guarantee is that both are
            // visible and that the hairline — the stroke that carries meaning,
            // e.g. disabled marks — is always the stronger of the two.
            assertTrue("$themeName: outline on surface is $outline", outline > 1.0)
            assertTrue("$themeName: hairline on surface is $hairline", hairline > outline)
        }

        // The dark hairline does clear the graphic-object threshold.
        val darkHairline = ContrastRatio.between(TaffyPalette.dark.hairline, TaffyPalette.dark.surface)
        assertTrue("dark: hairline on surface is $darkHairline", darkHairline >= ContrastRatio.GRAPHIC_MINIMUM)

        // Dark is held tighter than light on both strokes. The old dark
        // outline measured 1.29:1 on the ground and was effectively invisible;
        // the re-derived one has to stay visible on the ground and, because
        // one stroke serves four surfaces now, on the lightest of them too.
        val dark = TaffyPalette.dark
        val darkOutline = ContrastRatio.between(dark.outline, dark.surface)
        assertTrue("dark: outline on surface is $darkOutline", darkOutline >= DARK_OUTLINE_ON_GROUND)
        val onSheet = ContrastRatio.between(dark.outline, dark.surfaceSheet)
        assertTrue("dark: outline on surfaceSheet is $onSheet", onSheet >= DARK_OUTLINE_ON_SHEET)
    }

    @Test
    fun `the dark surface ramp is four rising steps and the sheet clears the page`() {
        // The reported defect this pins shut: a bottom sheet the same colour
        // as the page behind it. The handoff gave dark three tones for four
        // roles — surfaceSunken was aliased onto surfaceRaised — and put the
        // sheet 1.152:1 above the ground, which is arithmetically the same
        // surface. Each joint below has to stay a visible step, and the sheet
        // has to stay unmistakably nearer than the page even before its scrim.
        val dark = TaffyPalette.dark
        val ramp = listOf(
            "surface" to dark.surface,
            "surfaceSunken" to dark.surfaceSunken,
            "surfaceRaised" to dark.surfaceRaised,
            "surfaceSheet" to dark.surfaceSheet,
        )
        for ((lower, upper) in ramp.zipWithNext()) {
            val step = ContrastRatio.between(lower.second, upper.second)
            // Strictly rising, not merely different: a ratio alone cannot say
            // which of two colours is the lighter one.
            assertTrue(
                "dark: ${upper.first} is not above ${lower.first}",
                ContrastRatio.between(upper.second, BLACK) > ContrastRatio.between(lower.second, BLACK),
            )
            assertTrue("dark: ${upper.first} over ${lower.first} is $step", step >= RAMP_STEP)
        }

        val sheetOverPage = ContrastRatio.between(dark.surfaceSheet, dark.surface)
        assertTrue("dark: surfaceSheet over surface is $sheetOverPage", sheetOverPage >= SHEET_SEPARATION)

        // And over the page as the eye actually meets it, dimmed by the
        // sheet's own scrim: 1.555:1 measured.
        val scrimmed = compositeOver(dark.scrim, dark.surface)
        val sheetOverScrim = ContrastRatio.between(dark.surfaceSheet, scrimmed)
        assertTrue("dark: surfaceSheet over the scrimmed page is $sheetOverScrim", sheetOverScrim >= SHEET_SEPARATION)
    }

    @Test
    fun `the dark neutrals stay near-neutral`() {
        // The other half of the report: the dark theme read burgundy. At the
        // luminance of a dark ground a warm bias of a few units is not a tint,
        // it is the whole visible character of the surface, and the handoff's
        // grounds carried six to eleven units of it. These bounds are a
        // ratchet on what was measured after re-derivation — surface 2,
        // surfaceSunken 3, surfaceRaised 3, surfaceSheet 3, outline 4 — so the
        // cast cannot come back a unit at a time. Warmth is not banned; a
        // visible hue is. The accent family is untouched and unbounded here,
        // because the amber is the identity.
        val dark = TaffyPalette.dark
        for ((name, value) in listOf(
            "surface" to dark.surface,
            "surfaceSunken" to dark.surfaceSunken,
            "surfaceRaised" to dark.surfaceRaised,
            "surfaceSheet" to dark.surfaceSheet,
            "outline" to dark.outline,
        )) {
            val spread = channelSpread(value)
            assertTrue("dark: $name spreads $spread channel units", spread <= NEUTRAL_SPREAD)
        }
    }

    @Test
    fun `both themes define all forty-two tokens and share only the by-design values`() {
        assertEquals(42, TaffyPalette.light.asPairs().size)
        assertEquals(42, TaffyPalette.dark.asPairs().size)
        assertEquals(
            TaffyPalette.light.asPairs().map { it.first },
            TaffyPalette.dark.asPairs().map { it.first },
        )

        // Dark is a design of its own, not an inversion. The exceptions are
        // the tokens the handoff defines once for both themes: the accent
        // family (accent, accentText, accentDeep, onAccent), the two
        // semantic hues (danger, positive), and — since decision 0103 — the
        // ribbon's solids, wells and ink, which are the mark's own and do not
        // move with the theme; only their washes do.
        val byDesign = listOf(
            "accent", "accentText", "accentDeep", "accentOn", "danger", "positive",
            "accentWell", "ribbonOne", "ribbonOneWell", "ribbonTwo", "ribbonTwoWell",
            "ribbonThree", "ribbonThreeWell", "ribbonOn",
        )
        val shared = TaffyPalette.light.asPairs()
            .zip(TaffyPalette.dark.asPairs())
            .filter { (light, dark) -> light.second == dark.second }
            .map { it.first.first }
        assertEquals(byDesign.sorted(), shared.sorted())
    }

    @Test
    fun `ribbon wells carry their ink in both themes`() {
        // A hub tile's glyph sits on a solid well in the lightened ribbon hue
        // (decision 0103). One ink serves all three wells and the amber one:
        // measured 7.16 / 7.72 / 8.45 on the ribbon wells and 10.67 on the
        // accent well. The solid ribbon hues themselves are deliberately not
        // asserted as text anywhere — on light paper they measure 4.39 /
        // 4.05 / 2.83 — which is why a ribbon hue is never text.
        for ((themeName, palette) in themes()) {
            for ((name, well) in listOf(
                "ribbonOneWell" to palette.ribbonOneWell,
                "ribbonTwoWell" to palette.ribbonTwoWell,
                "ribbonThreeWell" to palette.ribbonThreeWell,
            )) {
                val ratio = ContrastRatio.between(palette.ribbonOn, well)
                assertTrue("$themeName: ribbonOn on $name is $ratio", ratio >= WELL_INK)
            }
            val amber = ContrastRatio.between(palette.accentOn, palette.accentWell)
            assertTrue("$themeName: accentOn on accentWell is $amber", amber >= WELL_INK)
        }
    }

    @Test
    fun `text stays legible on a ribbon wash over a raised card`() {
        // The pairing a hub tile actually draws: a ribbon wash composited
        // over surfaceRaised, with the tile's own title and summary on top.
        // The light washes are held at 14% on purpose — at 20% textSecondary
        // measures 4.45 on the first two and fails — and the dark ones at 18%,
        // where the worst pair is textPrimary on the amber wash at 9.64.
        for ((themeName, palette) in themes()) {
            for ((name, wash) in listOf(
                "ribbonOneWash" to palette.ribbonOneWash,
                "ribbonTwoWash" to palette.ribbonTwoWash,
                "ribbonThreeWash" to palette.ribbonThreeWash,
                "accentWash" to palette.accentWash,
            )) {
                val ground = compositeOver(wash, palette.surfaceRaised)
                val primary = ContrastRatio.between(palette.textPrimary, ground)
                assertTrue("$themeName: textPrimary on $name over a card is $primary", primary >= PRIMARY_ON_WASH)
                val secondary = ContrastRatio.between(palette.textSecondary, ground)
                assertTrue(
                    "$themeName: textSecondary on $name over a card is $secondary",
                    secondary >= ContrastRatio.TEXT_MINIMUM,
                )
            }
        }
    }

    @Test
    fun `the ribbon washes are heavier in dark than in light`() {
        // The same rule dangerWash and privateTintWash already follow: a wash
        // that reads on white would vanish on a dark card, so dark carries
        // more of the hue. A ratchet on the pairing, not on a number.
        val light = TaffyPalette.light
        val dark = TaffyPalette.dark
        for ((name, pair) in listOf(
            "ribbonOneWash" to (light.ribbonOneWash to dark.ribbonOneWash),
            "ribbonTwoWash" to (light.ribbonTwoWash to dark.ribbonTwoWash),
            "ribbonThreeWash" to (light.ribbonThreeWash to dark.ribbonThreeWash),
        )) {
            val (lightWash, darkWash) = pair
            assertTrue("$name is not heavier in dark", (darkWash ushr 24) > (lightWash ushr 24))
        }
    }

    @Test
    fun `every token is fully opaque except the washes and the two dims`() {
        // The washes are composited over a surface at draw time. `scrim` and
        // `selection` are the same idea under different names: a scrim is a
        // dim over whatever is behind it, and a selection fill has to let the
        // text it covers stay legible. Neither would mean anything opaque.
        val translucent = setOf("scrim", "selection")
        for ((themeName, palette) in themes()) {
            for ((name, value) in palette.asPairs()) {
                val alpha = value ushr 24
                if (name.endsWith("Wash") || name in translucent) {
                    assertTrue("$themeName: $name alpha is $alpha", alpha in 1L until 0xFFL)
                } else {
                    assertEquals("$themeName: $name", 0xFFL, alpha)
                }
            }
        }
    }

    @Test
    fun `neither theme reuses a surface value`() {
        // A caught-in-review guard. It used to exempt dark, where the handoff
        // collapsed surfaceSunken onto surfaceRaised; that collapse was the
        // layering defect rather than a design, so the exemption is gone and
        // both themes are now held to the same four distinct surfaces.
        for ((themeName, palette) in themes()) {
            val surfaces = listOf(
                "surface" to palette.surface,
                "surfaceSunken" to palette.surfaceSunken,
                "surfaceRaised" to palette.surfaceRaised,
                "surfaceSheet" to palette.surfaceSheet,
            )
            for ((lower, upper) in surfaces.zipWithNext()) {
                assertNotEquals("$themeName: ${lower.first} equals ${upper.first}", lower.second, upper.second)
            }
            assertNotEquals("$themeName: surface equals surfaceRaised", palette.surface, palette.surfaceRaised)
            assertNotEquals("$themeName: surface equals surfaceSheet", palette.surface, palette.surfaceSheet)
            assertNotEquals(
                "$themeName: surfaceSunken equals surfaceSheet",
                palette.surfaceSunken,
                palette.surfaceSheet,
            )
        }
    }

    // The four assertions below arrived with the fork's own copy of this suite
    // and are kept here, where the one palette now lives. The fork used to
    // compile a second `TaffyPalette` under
    // `//taffy/app/android/java/src/org/chromium/taffy/
    // designsystem/`, with the same 62 values and a contrast suite that had
    // grown four checks this one never had. That copy is gone (decision 0031
    // section 1); its extra checks are these, so retiring the duplicate costs
    // no coverage rather than quietly costing four assertions.

    @Test
    fun `inverse text is legible on the inverse surface in both themes`() {
        // The toast pairing, and the one pairing no other test can reach:
        // `textInverse` is never drawn on any of the four ordinary surfaces
        // and `surfaceInverse` never carries any other ink, so if these two
        // are not measured against each other they are not measured at all.
        for ((themeName, palette) in themes()) {
            val ratio = ContrastRatio.between(palette.textInverse, palette.surfaceInverse)
            assertTrue(
                "$themeName: textInverse on surfaceInverse is $ratio",
                ratio >= ContrastRatio.TEXT_MINIMUM,
            )
        }
    }

    @Test
    fun `selected text stays legible under the selection fill`() {
        // `selection` is translucent by design, so the colour the eye meets is
        // the fill composited over whichever surface the text sits on — and
        // the text that has just been selected is still the text a person is
        // reading.
        for ((themeName, palette) in themes()) {
            for ((background, value) in backgrounds(palette)) {
                val filled = compositeOver(palette.selection, value)
                val ratio = ContrastRatio.between(palette.textPrimary, filled)
                assertTrue(
                    "$themeName: textPrimary on selection over $background is $ratio",
                    ratio >= ContrastRatio.TEXT_MINIMUM,
                )
            }
        }
    }

    @Test
    fun `the focus ring clears non-text contrast on every surface`() {
        // A focus ring is a graphical object rather than text, so it is held
        // to 3:1 — but it is held to it on all four surfaces, because focus
        // lands wherever the control is and a ring that vanishes on a sheet is
        // a keyboard user losing their place.
        for ((themeName, palette) in themes()) {
            for ((background, value) in backgrounds(palette)) {
                val focus = ContrastRatio.between(palette.focusRing, value)
                assertTrue(
                    "$themeName: focusRing on $background is $focus",
                    focus >= ContrastRatio.GRAPHIC_MINIMUM,
                )
            }
        }
    }

    @Test
    fun `the scrim darkens what is behind it and the sheet stays above it`() {
        // The dark ramp test already checks the sheet against the scrimmed
        // page in dark. This is the other half and holds in both themes: that
        // the scrim actually *dims* — a ratio cannot say so, only luminance
        // can — and that putting it down always increases the separation
        // between the sheet and the page rather than flattening it.
        for ((themeName, palette) in themes()) {
            val scrimmed = compositeOver(palette.scrim, palette.surface)
            val plain = ContrastRatio.relativeLuminance(palette.surface)
            val dimmed = ContrastRatio.relativeLuminance(scrimmed)
            assertTrue(
                "$themeName: scrim takes surface luminance from $plain to $dimmed",
                dimmed <= plain / 2.0,
            )

            // Contrast arithmetic cannot separate two near-blacks, so the rule
            // that holds in both themes is a relative one. Light measures
            // 5.911 against 1.043, dark 1.555 against 1.473.
            val overScrim = ContrastRatio.between(palette.surfaceSheet, scrimmed)
            val overSurface = ContrastRatio.between(palette.surfaceSheet, palette.surface)
            assertTrue(
                "$themeName: sheet over scrim is $overScrim, over bare surface $overSurface",
                overScrim > overSurface,
            )
        }
    }

    private fun themes() = listOf("light" to TaffyPalette.light, "dark" to TaffyPalette.dark)

    private fun backgrounds(palette: PaletteValues) = listOf(
        "surface" to palette.surface,
        "surfaceRaised" to palette.surfaceRaised,
        "surfaceSunken" to palette.surfaceSunken,
        // A sheet is a surface a person reads on, and until it was added here
        // it was the one surface no text token had ever been measured against
        // — which is how it came to sit a bare 1.152:1 above the page in dark.
        "surfaceSheet" to palette.surfaceSheet,
    )

    /** The widest gap between any two channels: how far a neutral is from grey. */
    private fun channelSpread(color: Long): Long {
        val red = (color shr 16) and 0xFF
        val green = (color shr 8) and 0xFF
        val blue = color and 0xFF
        return maxOf(red, green, blue) - minOf(red, green, blue)
    }

    /** The opaque colour a translucent foreground produces over a background. */
    private fun compositeOver(foreground: Long, background: Long): Long {
        val alpha = foreground ushr 24
        fun channel(shift: Int): Long =
            (((foreground shr shift) and 0xFF) * alpha + ((background shr shift) and 0xFF) * (0xFFL - alpha)) / 0xFF
        return 0xFF000000L or
            (channel(16) shl 16) or
            (channel(8) shl 8) or
            channel(0)
    }

    private companion object {
        /** Opaque black, to order two colours by luminance through a ratio. */
        const val BLACK = 0xFF000000L

        /** Heading text on the surface most of a screen is. Measured 16.31 dark, 16.97 light. */
        const val PRIMARY_ON_GROUND = 13.0

        /** Heading text on a card, a well or a sheet. The binding case is the dark sheet, at 11.07. */
        const val PRIMARY_ON_LAYER = 10.5

        /** Dark secondary copy. Was 5.95 on a card and reported unreadable; now 7.36 at worst. */
        const val SUPPORTING_COMFORT = 7.0

        /** Dark tertiary copy. Was 4.51 on a card — conforming by a hundredth; now 4.83 at worst. */
        const val TERTIARY_COMFORT = 4.6

        /** A joint in the dark surface ramp, so a layer never merges into its neighbour. */
        const val RAMP_STEP = 1.12

        /** A sheet against the page it covers, scrim or no scrim. Measured 1.473 and 1.555. */
        const val SHEET_SEPARATION = 1.45

        /** The dark component boundary on the ground. Was 1.29 and invisible; now 1.681. */
        const val DARK_OUTLINE_ON_GROUND = 1.5

        /** The same stroke on the lightest dark surface, where it has least to work with: 1.142. */
        const val DARK_OUTLINE_ON_SHEET = 1.1

        /** Channel units a dark neutral may spread before it reads as a hue rather than warmth. */
        const val NEUTRAL_SPREAD = 4L

        /** A glyph on a lightened ribbon well. Measured 7.16 at worst; held well above the floor. */
        const val WELL_INK = 6.5

        /** Heading text on a ribbon wash over a card. Measured 9.64 at worst (dark, amber). */
        const val PRIMARY_ON_WASH = 8.5
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.ui.graphics.Color
import kotlin.math.pow

/**
 * The vendor-owned colours the vendored provider marks draw in, and the rule
 * that decides whether a recorded colour may be drawn at all.
 *
 * Here rather than in `TaffyTheme.colors`, and for a reason that outlived the
 * object that used to state it: a vendor's brand value is part of the
 * specification of somebody else's mark, and a value like that inside the
 * palette invites a later reader to reuse it as a product token. Most values
 * below are a distributor's recorded hex at the pin the path data came from —
 * some read out of Simple Icons' `hex` field, some out of the `fill` of the
 * colour variant lobe-icons publishes beside the same monochrome file — and the
 * two blacks for OpenAI and xAI are neither, because no distributor records a
 * value for either mark: they are a reading of the owner's own stated rule.
 * `core/ui/vendor/provider-marks.txt` records every one with which of the three
 * it is and where it came from.
 *
 * Keyed by the catalog's provider id rather than by an enum, because the list
 * itself is the served catalog's now (decision 0080): a provider this file
 * has never heard of gets `null`, which the badge draws as the product's own
 * monogram treatment. Marks stay compiled — a logo needs the provenance and
 * checksum decision 0030 requires, which a served document cannot carry.
 *
 * ## The chip, and why the decision is measured rather than listed
 *
 * The badge's own ground is `surfaceSunken`, which is near-white on the light
 * theme and near-black on the dark one, so no single fixed ink works for a mark
 * that is dark. Anthropic's `#191919` measures 14.83:1 on the light ground and
 * **1.02:1** on the dark one — not a low ratio, but the mark and its ground
 * being the same colour, which is a mark that has disappeared. Every vendor
 * colour therefore draws on a white chip of the badge's own size, in both
 * themes, which is also the ground each of the dark brands specifies for its
 * own mark, and one treatment for all of them rather than a different answer
 * per vendor.
 *
 * That fixes the dark half and opens the light half: a pale vendor colour on a
 * white chip is white on white, which is the same failure wearing the other
 * theme. So [readsOnChip] measures, and it is a **threshold, not a denylist**.
 * A hue that cannot clear the accessibility floor for a graphical object
 * against the chip is not drawn at all: [inkFor] answers `null` and the mark
 * takes the product's own ink on the product's own ground, legible in both
 * themes and claiming nothing. Darkening or lightening the hue to make it fit
 * would be publishing a brand colour its owner has not, which is exactly what
 * decision 0030 forbids for a mark.
 *
 * One of the twelve hues recorded below fails today, and it fails on the light
 * side: OpenRouter's slate `#94A3B8` at 2.56:1. Its hex stays recorded so the
 * next reader can see the value was read and rejected rather than never looked
 * up. The rest measure 3.36:1 (DeepSeek) to 21.00:1 (the five blacks) against
 * the chip. There were two until decision
 * `docs/decisions/0214-the-cloudflare-row-leaves-and-retrieval-becomes-owed.md`
 * took the Cloudflare row out of the catalog, and its orange `#F38020` at
 * 2.65:1 went with the mark rather than staying recorded here: a rejected hue
 * belongs to a mark this file may draw, and there is no such mark now.
 *
 * ## Together AI, which is recorded elsewhere and absent here on purpose
 *
 * One vendor is neither drawn nor rejected by the measurement, which is a third
 * outcome and not a variation on the second. `together-color.svg` at the pinned
 * version carries three fills, one per petal — `#EF2CC1`, `#CAAEF5`, `#FC4C02`
 * — and [recordedHue] holds one value per id. Two of the three clear the floor
 * on their own (3.62:1 and 3.40:1) and the pale one does not (1.93:1), but the
 * arithmetic is not what decides it: painting all three petals in one petal's
 * fill would be a re-drawing, which decision 0030 forbids and which is why the
 * three paths were transcribed separately in the first place. So the mark keeps
 * the product's own ink, and the values stay recorded in
 * `core/ui/vendor/provider-marks.txt` — read and rejected, not never looked up.
 * Adding a `"together"` arm below would draw the whole mark in one petal's
 * fill and would satisfy every other assertion in this module, so
 * `ProviderMarkResolutionTest` names the absence and fails on it rather than
 * leaving it to look like an oversight.
 */
internal object ProviderMarkStyle {

    /** The ground the marks that keep a vendor colour are drawn on, both themes. */
    val chip: Color = color(CHIP)

    /**
     * The vendor's own ink for this provider, or `null` to draw in the
     * product's tint on the product's ground.
     *
     * `null` is an answer rather than an omission. It means one of four
     * things, and the caller treats them alike because they end the same way:
     * no colour was ever recorded for this vendor — Groq and Baseten, whose
     * monochrome files are `currentColor` and for which the distributor
     * publishes no colour variant at the pin, both checked rather than assumed;
     * the recorded colour cannot be read on the chip it would sit on;
     * the vendor's recorded colour is a set of fills rather than one value, so
     * this table does not carry it (Together AI, above); or there is no
     * vendored mark for this provider at all.
     */
    fun inkFor(providerId: String): Color? =
        recordedHue(providerId)?.takeIf(::readsOnChip)?.let(::color)

    /**
     * The recorded hex for this provider's mark, drawn or not.
     *
     * Visible so a test can assert the measurement rather than the outcome: a
     * hue recorded here and rejected by [readsOnChip] is a deliberate state,
     * and one recorded here and silently drawn white-on-white would not be.
     */
    fun recordedHue(providerId: String): Long? = when (providerId) {
        "anthropic" -> 0x19_19_19
        // The rings' own fill in `cerebras-color.svg`; the letter path beside
        // them carries no fill and inherits the current colour, so a single
        // tint in this hue is what that file paints rather than a re-drawing.
        "cerebras" -> 0xF1_5A_29
        "deepseek" -> 0x57_86_FE
        "fireworks" -> 0x50_19_C5
        "github-copilot" -> 0x00_00_00
        "google-ai-studio" -> 0x8E_75_B2
        "kimi-coding" -> 0x00_00_00
        "minimax" -> 0xE7_35_62
        "moonshot" -> 0x00_00_00
        "openai" -> 0x00_00_00
        // Recorded and rejected — see the class comment.
        "openrouter" -> 0x94_A3_B8
        "xai" -> 0x00_00_00
        // No `"together"` arm, on purpose: its recorded colour is three fills
        // and this table holds one value. See the class comment — the values
        // are read and rejected, not never looked up. `"groq"` and `"baseten"`
        // are absent for the older reason: nothing was published to record.
        else -> null
    }

    /**
     * Whether a recorded hue is legible on [chip], in both themes at once.
     *
     * The chip is the same colour in both themes, so one measurement settles
     * both. `3:1` is the accessibility floor for a meaningful graphical object.
     */
    fun readsOnChip(hue: Long): Boolean = contrastAgainstChip(hue) >= GRAPHIC_MINIMUM

    /** The ratio, from 1 to 21, of one recorded hue against [chip]. */
    fun contrastAgainstChip(hue: Long): Double {
        val brighter = maxOf(relativeLuminance(hue), relativeLuminance(CHIP))
        val darker = minOf(relativeLuminance(hue), relativeLuminance(CHIP))
        return (brighter + 0.05) / (darker + 0.05)
    }

    private fun color(word: Long): Color = Color(OPAQUE or word)

    // The same formula `core/designsystem`'s `ContrastRatio` carries, written
    // again because that object is `internal` to its own module and this one
    // cannot see it. Nine lines of published arithmetic duplicated is the
    // smaller defect; widening another module's internals to reach it would
    // make a palette detail part of a public surface.
    private fun relativeLuminance(word: Long): Double {
        val red = channel(word shr 16)
        val green = channel(word shr 8)
        val blue = channel(word)
        return 0.2126 * red + 0.7152 * green + 0.0722 * blue
    }

    private fun channel(shifted: Long): Double {
        val value = (shifted and 0xFF).toDouble() / 255.0
        return if (value <= 0.03928) value / 12.92 else ((value + 0.055) / 1.055).pow(2.4)
    }
}

/** The chip's own colour word, white in both themes. */
private const val CHIP: Long = 0xFF_FF_FF

/** The alpha every colour word here is drawn at. */
private const val OPAQUE: Long = 0xFF_00_00_00

/** The accessibility floor for a meaningful graphical object. */
private const val GRAPHIC_MINIMUM: Double = 3.0

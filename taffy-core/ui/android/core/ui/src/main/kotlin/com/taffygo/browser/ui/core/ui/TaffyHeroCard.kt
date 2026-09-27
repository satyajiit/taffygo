// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** 2:3 cutout width on a stacked / high-scale phone. */
val TaffyHeroArtStacked: Dp = 158.dp

/** 2:3 cutout width below 390 dp. */
val TaffyHeroArtCompact: Dp = 192.dp

/** 2:3 cutout width on a 390+ phone. */
val TaffyHeroArtWide: Dp = 214.dp

/** Height is width × 1.5 so the figure stays 2:3. */
const val TaffyHeroArtHeightFactor: Float = 1.5f

/** Copy is 100% wide and the figure sits below under this width: the grid's own fold. */
const val TaffyHeroStackedWidthDp: Int = TaffyBentoColumns.FOLD_WIDTH_DP

/** Compact 2:3 cutout under this width, wide at or above it. */
const val TaffyHeroCompactWidthDp: Int = 390

/** Copy stacks at or above this font scale: the grid's own fold. */
const val TaffyHeroStackedFontScale: Float = TaffyBentoColumns.FOLD_FONT_SCALE

/** Height of a 2:3 hero cutout at [width]. */
fun taffyHeroArtHeight(width: Dp): Dp = width * TaffyHeroArtHeightFactor

/** Whether this window should stack copy above the figure. */
@Composable
fun taffyHeroStacked(): Boolean {
    return isTaffyHeroStacked(
        windowWidth = taffyHeroWindowWidth(),
        fontScale = LocalConfiguration.current.fontScale,
    )
}

/** 2:3 cutout width for this window. */
@Composable
fun taffyHeroArtWidth(): Dp {
    return taffyHeroArtWidth(
        windowWidth = taffyHeroWindowWidth(),
        fontScale = LocalConfiguration.current.fontScale,
    )
}

/**
 * The destination-canvas hero: radius 24, a surface wash, hanging blobs, a
 * copy column, an optional trailing clay mark, or a 2:3 character that
 * bleeds off the bottom-right. Copy stays on the wash. The illustration
 * sits in a slot at the end and does not fill the card.
 *
 * [wash] is the accent wash unless a hub lends the hero its ribbon hue
 * (decision 0103). The eyebrow is amber on an amber wash; a ribbon hero
 * passes [eyebrowColor] as the ordinary secondary ink, because a ribbon hue
 * never carries text and an amber word on a blue ground would say Taffy
 * where Taffy is not.
 */
@Composable
fun TaffyHeroCard(
    title: String,
    modifier: Modifier = Modifier,
    eyebrow: String? = null,
    body: String? = null,
    wash: Color = TaffyTheme.colors.accentWash,
    eyebrowColor: Color? = null,
    testTag: String? = null,
    illustration: Painter? = null,
    illustrationSize: Dp = IllustrationSlot,
    footer: @Composable (() -> Unit)? = null,
    character: @Composable (BoxScope.() -> Unit)? = null,
) {
    val useCharacter = character != null && illustration == null
    val windowWidth = taffyHeroWindowWidth()
    val fontScale = LocalConfiguration.current.fontScale
    val stacked = useCharacter && isTaffyHeroStacked(windowWidth, fontScale)
    val compact = windowWidth < TaffyHeroCompactWidthDp.dp
    val artWidth = if (useCharacter) taffyHeroArtWidth(windowWidth, fontScale) else 0.dp
    val shape = TaffyTheme.shapes.hero
    Box(
        modifier = modifier
            .fillMaxWidth()
            .padding(bottom = if (useCharacter) CharacterBottom else 0.dp)
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier),
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(
                    min = when {
                        illustration != null -> maxOf(
                            HeroMinIllustration,
                            illustrationSize + IllustrationFloorPad,
                        )
                        useCharacter -> HeroMinWithCharacter
                        else -> HeroMinPlain
                    },
                ),
        ) {
            Box(
                modifier = Modifier
                    .matchParentSize()
                    .clip(shape)
                    .background(wash)
                    .border(TaffyBorders.standard, TaffyTheme.colors.outline, shape),
            ) {
                Box(
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .offset(x = BlobLargeOffset, y = BlobLargeTop)
                        .size(BlobLarge)
                        .clip(CircleShape)
                        .background(TaffyTheme.colors.surfaceRaised.copy(alpha = BlobLargeAlpha)),
                )
                Box(
                    modifier = Modifier
                        .align(Alignment.BottomEnd)
                        .offset(x = BlobSmallEnd, y = BlobSmallBottom)
                        .size(BlobSmall)
                        .clip(CircleShape)
                        .background(TaffyTheme.colors.surfaceRaised.copy(alpha = BlobSmallAlpha)),
                )
                if (illustration != null) {
                    Image(
                        painter = illustration,
                        contentDescription = null,
                        contentScale = ContentScale.Fit,
                        modifier = Modifier
                            .align(Alignment.BottomEnd)
                            .padding(
                                end = TaffyTheme.spacing.tight,
                                bottom = TaffyTheme.spacing.tight,
                            )
                            .size(illustrationSize),
                    )
                }
            }
            Column(
                modifier = Modifier
                    .fillMaxWidth(if (useCharacter && !stacked) CopyColumnFraction else 1f)
                    .padding(TaffyTheme.spacing.cardPadding)
                    .padding(
                        end = if (illustration != null) illustrationSize else 0.dp,
                        bottom = if (stacked) {
                            taffyHeroArtHeight(artWidth) * StackedCopyLift
                        } else {
                            0.dp
                        },
                    ),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            ) {
                if (eyebrow != null) {
                    Text(
                        text = eyebrow,
                        style = TaffyTheme.typography.micro,
                        color = eyebrowColor ?: if (TaffyTheme.isDark) {
                            TaffyTheme.colors.accentText
                        } else {
                            TaffyTheme.colors.accentDeep
                        },
                    )
                }
                Text(
                    text = title,
                    style = TaffyTheme.typography.headline,
                    color = TaffyTheme.colors.textPrimary,
                )
                if (body != null) {
                    Text(
                        text = body,
                        style = TaffyTheme.typography.detail,
                        color = TaffyTheme.colors.textSecondary,
                    )
                }
                footer?.invoke()
            }
            if (useCharacter) {
                Box(
                    modifier = Modifier
                        .align(Alignment.BottomEnd)
                        .offset(
                            x = when {
                                stacked -> CharacterStackedEnd
                                compact -> CharacterCompactEnd
                                else -> CharacterEnd
                            },
                            y = CharacterBottom,
                        ),
                    content = character,
                )
            }
        }
    }
}

/** The tag a hero card carries, so a semantics test names it once. */
const val HERO_CARD_TEST_TAG: String = "hero_card"

@Composable
private fun taffyHeroWindowWidth(): Dp {
    val width = LocalWindowInfo.current.containerSize.width
    return with(LocalDensity.current) { width.toDp() }
}

private fun isTaffyHeroStacked(windowWidth: Dp, fontScale: Float): Boolean =
    TaffyBentoColumns.folds(windowWidth.value.toInt(), fontScale)

private fun taffyHeroArtWidth(windowWidth: Dp, fontScale: Float): Dp = when {
    isTaffyHeroStacked(windowWidth, fontScale) -> TaffyHeroArtStacked
    windowWidth < TaffyHeroCompactWidthDp.dp -> TaffyHeroArtCompact
    else -> TaffyHeroArtWide
}

private val HeroMinWithCharacter = 208.dp
private val HeroMinIllustration = 128.dp
private val HeroMinPlain = 96.dp
private val IllustrationSlot = 120.dp
private val IllustrationFloorPad = 16.dp
private val BlobLarge = 220.dp
private val BlobSmall = 104.dp
private val BlobLargeOffset = 72.dp
private val BlobLargeTop = (-92).dp
private val BlobSmallEnd = (-36).dp
private val BlobSmallBottom = 44.dp
private const val BlobLargeAlpha = 0.46f
private const val BlobSmallAlpha = 0.35f
private const val CopyColumnFraction = 0.61f
private val CharacterStackedEnd = 2.dp
private val CharacterCompactEnd = 32.dp
private val CharacterEnd = 24.dp
private val CharacterBottom = 30.dp
private const val StackedCopyLift = 0.55f

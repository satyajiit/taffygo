// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.taffyPhase

/**
 * The address pill (`handoff/TgAddressPill.dc.html`): the page's address, its
 * security mark, the blocker's count, and reload — one recessed row of
 * chrome, carried in screen SCR-101's top bar since decision 0119.
 *
 * The component is stateless: the caller owns the URL text, the blocked
 * count, whether the connection is private, and the site's mark. The lock is
 * green only on a private connection. The shield and its count hide
 * themselves at zero.
 *
 * ## Where a page's loading progress is shown, and why it is here
 *
 * On the chrome, never on the page. `docs/design/ux-spec.md` §2 gives the
 * content area to the web and says it is never covered by Taffy's UI, so the
 * previous page stays legible and scrollable while the next one arrives —
 * which is what every browser does and what a placeholder over the surface
 * took away. The pill is the right piece of chrome because it is already the
 * thing that changed: the address in it is the page being fetched.
 *
 * The rail is [TaffyBorders.rail] of `textPrimary` and it is **indeterminate**
 * by construction. `NavigationState` carries `isLoading` and no fraction, so a
 * bar that filled would be inventing one; a band that travels says work is
 * happening and claims nothing about how much is left. It is not amber:
 * `handoff/DESIGN.md`'s second principle reserves the accent for Taffy itself,
 * and a page loading is the browser working, not the assistant.
 *
 * Under [TaffyTheme.reducedMotion] the rail draws full width and still. It is
 * present and unambiguous without moving in the corner of the eye, which is
 * what ux-spec §12.5 asks for — the alternative, drawing nothing, would take
 * the only loading signal away from the people the rule exists to protect.
 */
@Composable
fun TaffyAddressPill(
    url: String,
    blockedCount: Int,
    modifier: Modifier = Modifier,
    isLoading: Boolean = false,
    isSecure: Boolean = false,
    favicon: Bitmap? = null,
    onReload: (() -> Unit)? = null,
    onStopLoading: (() -> Unit)? = null,
    onBlockedBadge: (() -> Unit)? = null,
) {
    val still = TaffyTheme.reducedMotion
    val phase = taffyPhase(running = isLoading && !still, periodMillis = RailSweepMillis)
    val rail = TaffyTheme.colors.textPrimary
    val pageAction = if (isLoading) onStopLoading else onReload
    val pageActionIcon = if (isLoading) TaffyIcon.StopCircle else TaffyIcon.ArrowClockwise
    // Named only when it is a control. A pill drawn with neither callback —
    // screen SCR-110's read-only origin is the first such caller — still draws
    // the glyph, dimmed, because the mark says what kind of thing the pill is;
    // but announcing "Reload page" for a box nothing happens on was reported
    // from a phone as a control that does nothing. Decorative marks carry no
    // description.
    val pageActionDescription = if (pageAction == null) {
        null
    } else {
        taffyString(
            if (isLoading) R.string.taffy_address_pill_stop else R.string.taffy_address_pill_reload,
        )
    }
    Row(
        modifier = modifier
            .height(PillHeight)
            .clip(TaffyTheme.shapes.pill)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(
                TaffyBorders.standard,
                TaffyTheme.colors.outline,
                TaffyTheme.shapes.pill,
            )
            // After the clip, so the band takes the pill's own rounded ends
            // rather than running past them, and after the border so it is not
            // drawn under the stroke it sits against.
            .drawWithContent {
                drawContent()
                if (!isLoading) return@drawWithContent
                val thickness = TaffyBorders.rail.toPx()
                val strip = Offset(0f, size.height - thickness)
                val stripSize = Size(size.width, thickness)
                if (still) {
                    drawRect(color = rail, topLeft = strip, size = stripSize)
                    return@drawWithContent
                }
                // TaffySkeleton's band arithmetic: the highlight enters one
                // band-width before the left edge and leaves one past the
                // right, so neither end of the travel shows a standing start.
                val band = size.width * RailBandFraction
                val x = phase.value * (size.width + band + band) - band
                drawRect(
                    brush = Brush.linearGradient(
                        colors = listOf(Color.Transparent, rail, Color.Transparent),
                        start = Offset(x, 0f),
                        end = Offset(x + band, 0f),
                    ),
                    topLeft = strip,
                    size = stripSize,
                )
            }
            .padding(horizontal = PillPadding),
        horizontalArrangement = Arrangement.spacedBy(PillGap),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = TaffyIcon.LockSimple,
            contentDescription = taffyString(
                if (isSecure) {
                    R.string.taffy_address_pill_secure
                } else {
                    R.string.taffy_address_pill_insecure
                },
            ),
            modifier = Modifier.size(LockSize),
            tint = if (isSecure) {
                TaffyTheme.colors.positive
            } else {
                TaffyTheme.colors.textSecondary
            },
        )
        if (favicon != null) {
            Image(
                bitmap = favicon.asImageBitmap(),
                contentDescription = null,
                contentScale = ContentScale.Crop,
                modifier = Modifier
                    .size(FaviconSize)
                    .clip(CircleShape),
            )
        }
        Text(
            text = url,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f),
        )
        if (blockedCount > 0) {
            val blocked = taffyPlural(R.plurals.taffy_blocked_trackers, blockedCount, blockedCount)
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    // The badge is the way into the site sheet when the host
                    // offers one; a pill drawn without the callback keeps the
                    // badge as the plain fact it always was.
                    .then(
                        if (onBlockedBadge != null) {
                            Modifier.clickable(onClick = onBlockedBadge)
                        } else {
                            Modifier
                        },
                    )
                    .semantics(mergeDescendants = true) {
                        contentDescription = blocked
                    },
            ) {
                Icon(
                    imageVector = TaffyIcon.ShieldCheck,
                    contentDescription = null,
                    modifier = Modifier.size(ShieldSize),
                    tint = TaffyTheme.colors.textSecondary,
                )
                Text(
                    text = taffyCount(blockedCount),
                    // Between the label and numeric roles: the numeric role's
                    // tabular figures at the spec's chip size.
                    style = TaffyTheme.typography.numeric.copy(fontSize = BlockedCountSize),
                    color = TaffyTheme.colors.textSecondary,
                    modifier = Modifier.padding(start = TaffyTheme.spacing.step),
                )
            }
        }
        Box(
            modifier = Modifier
                .size(ReloadTarget)
                .then(if (pageAction != null) Modifier.clickable(onClick = pageAction) else Modifier),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                imageVector = pageActionIcon,
                contentDescription = pageActionDescription,
                modifier = Modifier.size(ReloadSize),
                // Ink, not a border. This was `outline` — the token for the
                // hairline around the pill — and a glyph painted in the colour
                // a one-pixel rule is drawn in is a glyph nobody can see: it
                // was reported from a phone as reload being invisible. The
                // shield beside it already uses [textSecondary], which is the
                // token for a mark that is present but not the point, and
                // that is exactly what this is.
                tint = if (pageAction != null) {
                    TaffyTheme.colors.textSecondary
                } else {
                    TaffyTheme.colors.textTertiary
                },
            )
        }
    }
}

// The spec's geometry (handoff/TgAddressPill.dc.html; px read as dp).
private val PillHeight = 44.dp
private val PillPadding = 15.dp
private val PillGap = 9.dp
private val LockSize = 12.dp
private val FaviconSize = 16.dp
private val ShieldSize = 11.dp
private val BlockedCountSize = 11.sp
private val ReloadSize = 15.dp

/** Reload keeps a full touch target even though its glyph renders smaller. */
private val ReloadTarget = 44.dp

/** The travelling band, matching `TaffySkeleton`'s proportions and pace. */
private const val RailBandFraction = 0.4f
private const val RailSweepMillis = 1_200

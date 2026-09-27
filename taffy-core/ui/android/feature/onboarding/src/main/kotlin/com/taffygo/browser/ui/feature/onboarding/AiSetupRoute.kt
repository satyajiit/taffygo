// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.ui.graphics.vector.ImageVector
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.ui.TaffyIcon

/**
 * A way a model request can leave this phone, as the screen draws it.
 *
 * There is one, and it is the person's own account at their own provider.
 * TaffyGo operates no service of its own to offer beside it (decision
 * `docs/decisions/0200-taffygo-operates-no-servers.md`), so the screen is no
 * longer a comparison between a paid route and a free one — it is one route
 * and the option to answer later.
 *
 * This stays a list of one rather than collapsing into a constant. The route
 * is still a *choice* a person makes and the screen still records it, and a
 * second route — a local model on the device, say — is a row added here rather
 * than a screen rewritten. The card reads every word out of the data, so the
 * copy sits in one place and a translator sees the whole offer.
 */
internal data class AiSetupRoute(
    /** The value choosing this card writes into the preferences. */
    val route: ProviderRoute,

    /** The name of the route. */
    val title: Int,

    /** One sentence about what choosing it means. */
    val body: Int,

    /** What it costs, drawn as the pill under the title. */
    val cost: Int,

    /** The glyph on the card's tile, from the vendored Phosphor set. */
    val icon: ImageVector,
)

/** The routes on offer, in the order the screen offers them. */
internal val AiSetupRoutes: List<AiSetupRoute> = listOf(
    AiSetupRoute(
        route = ProviderRoute.DIRECT_WITH_YOUR_KEY,
        title = R.string.taffy_ai_setup_own_key_title,
        body = R.string.taffy_ai_setup_own_key_body,
        cost = R.string.taffy_ai_setup_own_key_cost,
        icon = TaffyIcon.Key,
    ),
)

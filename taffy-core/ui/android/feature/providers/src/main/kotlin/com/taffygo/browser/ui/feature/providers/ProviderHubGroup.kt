// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * The three ways in to a provider that screen SCR-404 divides by, and the
 * three tabs it draws them under.
 *
 * Three rather than one long list, because a person arrives at this screen
 * with one of three questions — what plan can I sign in to, where do I paste a
 * key, what have I set up myself — and a single ordered list answers none of
 * them without being read end to end.
 *
 * They are **tabs** rather than three stacked sections because the three are
 * alternatives rather than parts of one list: a person who came to paste a key
 * is not reading the plans. One category is on screen at a time and the row of
 * tabs says what the other two hold.
 *
 * ## There is no Connected group, and that is the point
 *
 * SCR-404 is where a provider is *added*; SCR-419 is where the ones already
 * working are managed. A fourth tab listing what is connected made this screen
 * both, and the cost was paid twice: a person who had set nothing up landed on
 * an empty tab explaining that it was empty, and a person who had set
 * something up read their own providers on a screen whose every other tab was
 * a shop. `ProviderRowDispatch.groupsFor` therefore files a provider with a
 * credential under **no** group at all — it is not something to add — and
 * `ConnectedProvidersProjection` picks up exactly the rows this one drops.
 *
 * The order of the constants is the order the tabs appear in.
 */
enum class ProviderHubGroup {
    /** A plan the person already pays for, reached by signing in to the vendor. */
    SUBSCRIPTION,

    /** A provider paid per token with a key the person pastes. */
    BRING_YOUR_OWN_KEY,

    /** A provider the person supplied the address of themselves. */
    YOUR_OWN_ENDPOINT,
}

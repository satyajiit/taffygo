// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/**
 * What Keep this can place in Library.
 *
 * These are kinds, not payloads. The destination carries no fact, page, file,
 * or workspace of its own; choosing a kind names what would be kept if the
 * port could keep anything.
 */
enum class KeepThisKind {
    /** A single fact Taffy already showed. */
    FACT,

    /** Words taken from a page, not the whole page as a bookmark. */
    PAGE_EXTRACT,

    /** A file the person pointed at. */
    FILE,

    /** A finished workspace. */
    WORKSPACE,
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

/**
 * Which profile a site exception is written to.
 *
 * A private tab keeps its own allowances: they live in the private profile's
 * plane, are never written to disk, and are gone when the last private tab
 * closes (decision 0128). So a caller has to say which plane it means, and
 * [BrowserRepository.setSiteFilteringException] takes this with **no default
 * value** on purpose — a default would make one of these two the quiet answer
 * at the call site where the other one is meant, which is exactly the defect
 * that record was written for.
 */
enum class SiteFilteringPlane {
    /**
     * The regular profile's own list — what screen SCR-206 shows and acts on.
     * Never the private plane, whatever tab happens to be selected behind the
     * settings screen.
     */
    PROFILE,

    /**
     * The plane the selected tab belongs to: the private profile's while a
     * private tab is selected, the regular profile's otherwise. This is what
     * the site sheet on SCR-204 means, because that sheet is about the page in
     * front of the person.
     */
    SELECTED_TAB,
}

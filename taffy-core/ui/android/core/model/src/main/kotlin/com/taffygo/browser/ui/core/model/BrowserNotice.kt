// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * Something the browser was asked to do, did not do, and owes the person an
 * explanation for.
 *
 * A refusal that leaves no trace is indistinguishable from a broken control:
 * the box closes, nothing happens, and the person is left to guess whether the
 * browser understood them. This type is the trace. It carries the *fact* of the
 * refusal and nothing else — the words belong to the screen that shows them,
 * because the words are a product decision and this module has none to give.
 *
 * A notice is transient. It describes the last thing that did not happen, it is
 * cleared when the person acknowledges it or does something else, and nothing
 * here is ever written down.
 */
enum class BrowserNotice(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /**
     * A search was committed and there is no address to open.
     *
     * The address bar uses the engine chosen in General (default Google,
     * decision 0019). This notice is only recorded when that choice cannot
     * produce an address. Which provider may ground a model remains OD-019.
     */
    NO_SEARCH_ENGINE("no_search_engine"),
}

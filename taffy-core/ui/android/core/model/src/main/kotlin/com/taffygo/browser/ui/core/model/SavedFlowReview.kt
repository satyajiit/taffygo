// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/** Display-only review of one immutable, browser-recorded definition. Contains no entered values. */
data class SavedFlowReview(
    val id: String,
    val version: UInt,
    val origin: String,
    val startingAddress: String,
    val steps: List<Step>,
) {
    /** A closed action description and any public address or named control it uses. */
    data class Step(
        val action: Action,
        val address: String? = null,
        val target: Target? = null,
        val personPurpose: PersonPurpose? = null,
    )

    enum class Action {
        OPEN_PAGE, READ_PAGE, FIND_CONTROL, CHOOSE_CONTROL, FOCUS_CONTROL, OPEN_LINK, SCROLL, HANDOVER,
        INSPECT_FORM, FILL_FORM, SUBMIT_FORM, DOWNLOAD, READ_DOWNLOADS, INSPECT_PDF,
        BACK, FORWARD, RELOAD, STOP_LOADING, OPEN_TAB,
    }

    /** Declaration order matches the contract's closed procedure phrase catalogue. */
    enum class Target {
        SIGN_IN, SIGN_OUT, SEARCH, ADD_TO_CART, VIEW_CART, CHECKOUT,
        CONTINUE, CONFIRM, CANCEL, NEXT_PAGE, DOWNLOAD,
    }

    /** A description of what the person supplies, never the value itself. */
    enum class PersonPurpose { IDENTITY_NUMBER, VERIFICATION, ONE_TIME_CODE, FORM_DETAILS }
}

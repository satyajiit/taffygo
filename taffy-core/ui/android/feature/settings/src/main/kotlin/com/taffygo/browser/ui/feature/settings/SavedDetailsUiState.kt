// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-414 — saved details. */
data class SavedDetailsUiState(
    val availability: YouSurfaceAvailability = YouSurfaceAvailability.READY,
    val people: List<SavedDetailsRepository.Person> = emptyList(),
    val editor: Editor? = null,
    val confirmDelete: Boolean = false,
) {
    /** Add or edit one person. No national-id field. */
    data class Editor(
        val id: String? = null,
        val givenName: String = "",
        val familyName: String = "",
        val email: String = "",
        val phone: String = "",
        val address: String = "",
        val postcode: String = "",
        val country: String = "",
    )
}

internal fun SavedDetailsUiState.Editor.toPerson(): SavedDetailsRepository.Person =
    SavedDetailsRepository.Person(
        id = id.orEmpty(),
        givenName = givenName.trim(),
        familyName = familyName.trim(),
        email = email.trim(),
        phone = phone.trim(),
        address = address.trim(),
        postcode = postcode.trim(),
        country = country.trim(),
    )

internal fun SavedDetailsRepository.Person.toEditor(): SavedDetailsUiState.Editor =
    SavedDetailsUiState.Editor(
        id = id,
        givenName = givenName,
        familyName = familyName,
        email = email,
        phone = phone,
        address = address,
        postcode = postcode,
        country = country,
    )

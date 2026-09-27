// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.ui.REGION_COUNTRY_TEST_TAG_PREFIX
import com.taffygo.browser.ui.core.ui.REGION_PICKER_SEARCH_TEST_TAG
import com.taffygo.browser.ui.core.ui.REGION_PICKER_TEST_TAG
import com.taffygo.browser.ui.core.ui.RegionPicker as SharedRegionPicker
import com.taffygo.browser.ui.core.ui.taffyString
import java.util.Locale

/** The complete country chooser, kept in the shared bottom-sheet frame. */
@Composable
internal fun RegionPicker(
    selectedRegionCode: String,
    query: String,
    locale: Locale,
    onQueryChange: (String) -> Unit,
    onChoose: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    SharedRegionPicker(
        selectedRegionCode = selectedRegionCode,
        query = query,
        locale = locale,
        title = taffyString(R.string.taffy_language_region_picker_title),
        body = taffyString(R.string.taffy_language_region_picker_body),
        searchPlaceholder = taffyString(R.string.taffy_language_region_picker_search),
        onQueryChange = onQueryChange,
        onChoose = onChoose,
        onDismiss = onDismiss,
        testTag = LANGUAGE_REGION_PICKER_TEST_TAG,
        searchTestTag = LANGUAGE_REGION_PICKER_SEARCH_TEST_TAG,
        countryTestTagPrefix = LANGUAGE_REGION_COUNTRY_TEST_TAG_PREFIX,
    )
}

const val LANGUAGE_REGION_PICKER_TEST_TAG: String = REGION_PICKER_TEST_TAG
const val LANGUAGE_REGION_PICKER_SEARCH_TEST_TAG: String = REGION_PICKER_SEARCH_TEST_TAG
const val LANGUAGE_REGION_COUNTRY_TEST_TAG_PREFIX: String = REGION_COUNTRY_TEST_TAG_PREFIX

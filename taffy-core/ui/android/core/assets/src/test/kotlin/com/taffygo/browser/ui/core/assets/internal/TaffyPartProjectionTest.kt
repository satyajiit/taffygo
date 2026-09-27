// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.assets.internal

import com.taffygo.browser.ui.core.model.TaffyConnectionCost
import com.taffygo.browser.ui.core.model.TaffyPartAvailability
import com.taffygo.browser.ui.core.model.TaffyPartHold
import com.taffygo.browser.ui.core.model.TaffyPartPurpose
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.AssetDeliveryView
import taffy.core_api.AssetKindView
import taffy.core_api.AssetNetworkCostView
import taffy.core_api.AssetPresenceView
import taffy.core_api.AssetRefusal
import taffy.core_api.AssetRefusalView
import taffy.core_api.AssetViewState

/** The one place a delivery verdict is renamed for a person. */
internal class TaffyPartProjectionTest {

    @Test
    fun `every contract verdict a person can act on keeps its own name`() {
        val distinct = mapOf(
            AssetRefusalView.NO_VARIANT_FOR_PLATFORM to
                TaffyPartHold.NOT_AVAILABLE_FOR_THIS_DEVICE,
            AssetRefusalView.NOT_PUBLISHED_YET to TaffyPartHold.NOT_PUBLISHED,
            AssetRefusalView.ATTEMPTS_EXHAUSTED to TaffyPartHold.ATTEMPTS_SPENT,
            AssetRefusalView.INTEGRITY_FAILED to TaffyPartHold.WRONG_CONTENTS,
            AssetRefusalView.NETWORK_NOT_PERMITTED to TaffyPartHold.CONNECTION_NOT_ALLOWED,
            AssetRefusalView.DECLINED_BY_PERSON to TaffyPartHold.TURNED_OFF,
        )

        distinct.forEach { (verdict, expected) ->
            assertEquals(expected, view(refusal = verdict).toUiParts().parts.single().hold)
        }
    }

    @Test
    fun `the three verdicts about our own catalog say so, and say nothing else`() {
        // A person can do nothing about any of them and the sentence is the
        // same, so they collapse. Collapsing further would lose the difference
        // between "your connection" and "our mistake".
        val ours = listOf(
            AssetRefusalView.UNKNOWN_ASSET,
            AssetRefusalView.CATALOG_ROW_INCOMPLETE,
            AssetRefusalView.VARIANT_TOO_LARGE,
        )

        ours.forEach { verdict ->
            val hold = view(refusal = verdict).toUiParts().parts.single().hold
            assertEquals(TaffyPartHold.PRODUCT_DEFECT, hold)
            assertFalse(requireNotNull(hold).canRetry)
        }
    }

    @Test
    fun `retryability is the plane's answer and is not worked out here`() {
        // The contract says this refusal is retryable, so the part is, even
        // though the same reason on another day may not be.
        val part = view(refusal = AssetRefusalView.NETWORK_NOT_PERMITTED)
            .toUiParts()
            .parts
            .single()

        assertTrue(part.canRetry)
    }

    @Test
    fun `a part with no refusal is holding for no reason at all`() {
        val part = view(refusal = null).toUiParts().parts.single()

        assertNull(part.hold)
        assertFalse(part.canRetry)
    }

    @Test
    fun `the connection facts are carried once, for every part at once`() {
        val projected = view(refusal = null).toUiParts()

        assertTrue(projected.supported)
        assertEquals(TaffyConnectionCost.MOBILE_DATA, projected.connectionCost)
    }

    @Test
    fun `what a part is for and how much of it is here both cross unchanged`() {
        val part = view(refusal = null).toUiParts().parts.single()

        assertEquals(TaffyPartPurpose.PYTHON_LIBRARY, part.purpose)
        assertEquals(TaffyPartAvailability.PARTIAL, part.availability)
        assertEquals(256L, part.downloadedBytes)
        assertEquals(1_024L, part.totalBytes)
        assertEquals(3, part.attempts)
    }

    @Test
    fun `an unrelated status publication reuses the complete delivery projection`() {
        val delivery = view(refusal = null)
        val baseline = status(delivery, generation = 1uL)

        assertEquals(
            baseline.partsProjectionVersion(),
            status(delivery, generation = 9uL).partsProjectionVersion(),
        )
        assertNotEquals(
            baseline.partsProjectionVersion(),
            status(delivery.copy(metered_permitted = true), generation = 1uL)
                .partsProjectionVersion(),
        )
    }

    private fun view(refusal: AssetRefusalView?) = AssetDeliveryView(
        platform_supported = true,
        network_cost = AssetNetworkCostView.METERED,
        metered_permitted = false,
        assets = listOf(
            AssetViewState(
                asset_id = "python-stdlib",
                asset_revision = "3.14.1-1",
                kind = AssetKindView.PYTHON_STDLIB,
                presence = AssetPresenceView.PARTIAL,
                written_bytes = 256uL,
                total_bytes = 1_024uL,
                attempts = 3u,
                refusal = refusal?.let {
                    AssetRefusal(reason = it, retryable = it.canRetryForTest())
                },
                waiting_until_monotonic_ms = 0uL,
            ),
        ),
    )

    /**
     * What the delivery plane would answer for this verdict.
     *
     * A fixture stands in for the plane here, so it states the plane's rule
     * rather than inventing one: a catalog defect and a person's decision
     * cannot be retried, and a network condition or spent attempts can.
     */
    private fun AssetRefusalView.canRetryForTest(): Boolean = when (this) {
        AssetRefusalView.ATTEMPTS_EXHAUSTED,
        AssetRefusalView.INTEGRITY_FAILED,
        AssetRefusalView.NETWORK_NOT_PERMITTED,
        -> true
        else -> false
    }

    private fun status(delivery: AssetDeliveryView?, generation: ULong) = taffy.core_api.CoreStatus(
        availability = taffy.core_api.CoreAvailability.READY,
        generation = generation,
        active_tasks = emptyList(),
        auth_state = null,
        workspaces = emptyList(),
        workspace_export = null,
        asset_delivery = delivery,
        provider_roster = emptyList(),
        provider_probes = emptyList(),
        provider_models = emptyList(),
        assistant_configuration = taffy.core_api.AssistantConfigurationView(
            0uL,
            emptyList(),
            taffy.core_api.PersonalityPresetView.CAREFUL_RESEARCHER,
            0u,
            1u,
            0u,
        ),
        library = taffy.core_api.LibraryViewState(
            taffy.core_api.LibraryAvailability.AVAILABLE,
            0uL,
            emptyList(),
            null,
            emptyList(),
            emptyList(),
        ),
        library_export = null,
        memory = taffy.core_api.MemoryViewState(
            taffy.core_api.MemoryAvailability.AVAILABLE,
            0uL,
            emptyList(),
            null,
        ),
        saved_sign_ins = taffy.core_api.SavedSignInsView(
            taffy.core_api.SavedDataAvailability.UNAVAILABLE,
            0uL,
            emptyList(),
        ),
        saved_details = taffy.core_api.SavedDetailsView(
            taffy.core_api.SavedDataAvailability.UNAVAILABLE,
            0uL,
            emptyList(),
        ),
        site_skills = emptyList(),
        builtin_skills = emptyList(),
        projection_mode = taffy.core_api.CoreStatusProjectionMode.COMPLETE,
        projection_omissions = emptyList(),
    )
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith
import taffy.core_api.TaskAttachedStore
import taffy.core_api.TaskConsentPreview
import taffy.core_api.TaskProviderRoute
import org.chromium.taffy.core_api.mojom.CoreApiSubmissionStatus as MojoSubmissionStatus

/**
 * The consent's mojom form, every field set so the generated encoder accepts
 * it; and the reason each refused submission crosses back as.
 */
@RunWith(BaseRobolectricTestRunner::class)
class CoreApiEndpointMappingTest {

    private fun consent(stores: List<TaskAttachedStore>) = TaskConsentPreview(
        source_hosts = listOf("uidai.gov.in"),
        source_discovery_enabled = true,
        new_source_cap = 8u,
        provider_route = TaskProviderRoute.DIRECT_USER_KEY,
        attached_stores = stores,
    )

    @Test
    fun aConsentWithNoStoreAttachedStillEncodes() {
        val mojo = consent(emptyList()).toMojo()

        // The first start after `attached_stores` was added left this array
        // null, the encoder threw, and the endpoint read the throw as a dead
        // transport. Serializing here is the assertion that no field is left
        // to its default.
        assertNotNull(mojo.serialize())
        assertArrayEquals(intArrayOf(), mojo.attachedStores)
        assertArrayEquals(arrayOf("uidai.gov.in"), mojo.sourceHosts)
        assertEquals(true, mojo.sourceDiscoveryEnabled)
        assertEquals(8, mojo.newSourceCap)
        assertEquals(TaskProviderRoute.DIRECT_USER_KEY.wire.toInt(), mojo.providerRoute)
    }

    @Test
    fun eachAttachedStoreCrossesAsItsWireValueInOrder() {
        val mojo = consent(listOf(TaskAttachedStore.HISTORY, TaskAttachedStore.OPEN_TABS)).toMojo()

        assertNotNull(mojo.serialize())
        assertArrayEquals(
            intArrayOf(TaskAttachedStore.HISTORY.wire.toInt(), TaskAttachedStore.OPEN_TABS.wire.toInt()),
            mojo.attachedStores,
        )
    }

    @Test
    fun aStartRefusedOverTabsOrWindowsKeepsItsOwnReason() {
        // Each of these used to be INVALID_REQUEST on the wire, which the
        // start line read as "Taffy could not read this request" (decision
        // 0231). A readable request refused over the person's tabs or windows
        // must not fold back into it here.
        assertEquals(
            CoreApiSubmissionException.Reason.SOURCE_NOT_OPEN,
            submissionFailure(MojoSubmissionStatus.SOURCE_NOT_OPEN),
        )
        assertEquals(
            CoreApiSubmissionException.Reason.SOURCE_AMBIGUOUS,
            submissionFailure(MojoSubmissionStatus.SOURCE_AMBIGUOUS),
        )
        assertEquals(
            CoreApiSubmissionException.Reason.WINDOW_UNAVAILABLE,
            submissionFailure(MojoSubmissionStatus.WINDOW_UNAVAILABLE),
        )
        assertEquals(
            CoreApiSubmissionException.Reason.INVALID_REQUEST,
            submissionFailure(MojoSubmissionStatus.INVALID_REQUEST),
        )
        assertEquals(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION, submissionFailure(Int.MAX_VALUE))
    }
}

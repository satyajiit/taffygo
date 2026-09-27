// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.taffy.browser.field_values.mojom.FieldChallengeKind
import org.chromium.taffy.browser.field_values.mojom.FieldHighlight
import org.chromium.taffy.browser.field_values.mojom.FieldValueDescriptor
import org.chromium.taffy.browser.field_values.mojom.FieldValueRefusal
import org.chromium.taffy.browser.field_values.mojom.FieldValueRefusalReason
import org.chromium.taffy.browser.field_values.mojom.FieldValueRequest
import org.chromium.taffy.browser.field_values.mojom.FieldValueSupplyVerdict
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumTaskInputEndpointTest {
    @Test
    fun `a browser description maps without value bytes`() {
        val request = request(
            field("second", FieldChallengeKind.ONE_TIME_CODE, masked = true),
            field("first", FieldChallengeKind.IMAGE_CHALLENGE).apply {
                challengeImage = byteArrayOf(1, 2, 3)
                highlight = FieldHighlight().apply {
                    left = 0.1f
                    top = 0.2f
                    right = 0.3f
                    bottom = 0.4f
                }
            },
        )

        val described = describeFieldValueRequest(request)

        requireNotNull(described)
        assertEquals("request-1", described.requestId)
        assertEquals("bank.test", described.host)
        assertEquals(listOf("second", "first"), described.fields.map { it.id })
        assertTrue(described.fields.first().sensitive)
        assertEquals("one_time_code", described.fields.first().challenge)
        assertEquals("image_challenge", described.fields.last().challenge)
        assertArrayEquals(byteArrayOf(1, 2, 3), described.fields.last().challengeImage)
        assertEquals(0.3f, described.fields.last().highlightRight)
    }

    @Test
    fun `answers are ordered by the browser descriptors not map iteration`() {
        val described = requireNotNull(describeFieldValueRequest(
            request(field("second"), field("first")),
        ))

        val ordered = orderedFieldValues(
            described,
            "request-1",
            linkedMapOf("first" to "one", "second" to "two"),
        )

        assertArrayEquals(arrayOf("two", "one"), ordered)
        assertNull(orderedFieldValues(described, "another-request", emptyMap()))
        assertNull(orderedFieldValues(described, "request-1", mapOf("first" to "one")))
    }

    @Test
    fun `unknown enums and duplicate fields refuse the whole request`() {
        assertNull(describeFieldValueRequest(request(field("field", challenge = 99))))
        assertNull(describeFieldValueRequest(request(field("same"), field("same"))))
    }

    @Test
    fun `browser supply terminals remain closed and content free`() {
        assertEquals(
            TaffyResult.Success(Unit),
            fieldValueSubmissionResult(FieldValueSupplyVerdict.ACCEPTED, emptyArray()),
        )
        val refusal = FieldValueRefusal().apply {
            fieldId = "field"
            reason = FieldValueRefusalReason.FIELD_MAY_NOT_BE_FILLED
        }
        assertEquals(
            TaffyResult.Failure(FailureReason.NOT_PERMITTED),
            fieldValueSubmissionResult(FieldValueSupplyVerdict.ACCEPTED, arrayOf(refusal)),
        )
        assertEquals(
            TaffyResult.Failure(FailureReason.PROTOCOL_VIOLATION),
            fieldValueSubmissionResult(99, emptyArray()),
        )
    }

    private fun request(vararg fields: FieldValueDescriptor) = FieldValueRequest().apply {
        requestId = "request-1"
        taskId = "task-1"
        host = "bank.test"
        this.fields = fields
    }

    private fun field(
        id: String,
        challenge: Int = FieldChallengeKind.NONE,
        masked: Boolean = false,
    ) = FieldValueDescriptor().apply {
        fieldId = id
        label = "Label"
        this.masked = masked
        this.challenge = challenge
    }
}

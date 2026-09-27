// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
package org.chromium.taffy.host

import java.lang.reflect.Proxy
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.mojo.bindings.Message
import org.chromium.mojo.bindings.MessageReceiver
import org.chromium.mojo.bindings.MessageReceiverWithResponder
import org.chromium.taffy.core_api.mojom.CoreApiSubmissionStatus
import org.chromium.taffy.core_api.mojom.SavedFlowQueryAvailability
import org.chromium.taffy.core_api.mojom.SavedFlowQueryResult
import org.chromium.taffy.core_api.mojom.SiteSkillArgumentKind
import org.chromium.taffy.core_api.mojom.SiteSkillObservedArgument
import org.chromium.taffy.core_api.mojom.SiteSkillObservedStep
import org.chromium.taffy.core_api.mojom.SiteSkillProvenanceView
import org.chromium.taffy.core_api.mojom.SiteSkillStatusView
import org.chromium.taffy.core_api.mojom.SiteSkillView
import org.chromium.taffy.core_api.mojom.TaffyProfileCoreApi
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith

/** Real generated Mojo proxy/stub encoding in both directions; no mocked DTO encoder. */
@RunWith(BaseRobolectricTestRunner::class)
class CoreApiSavedFlowWireTest {
    @Test
    fun findReviewAndOpenRoundTripTheirExactArgumentsAndCompleteReply() {
        val calls = mutableListOf<List<Any?>>()
        val ordinals = mutableListOf<Int>()
        val proxy = loopback(calls, ordinals)
        var answered = 0
        proxy.findSavedFlows("find", "Download my document") { reply ->
            val projected = projectSavedFlowQuery(reply, "find")
            assertNotNull(projected)
            assertEquals(7uL, projected!!.service_generation)
            assertEquals(2u, projected.flows.single().active_version)
            assertEquals("https://example.test/start", projected.flows.single().reviewed_steps.single().arguments.single().public_address)
            answered++
        }
        proxy.getSavedFlowReview("review", "flow.saved", 2) { reply ->
            assertNotNull(projectSavedFlowQuery(reply, "review"))
            answered++
        }
        proxy.openSavedFlowStart("open", "flow.saved", 2) { status ->
            assertEquals(CoreApiSubmissionStatus.ACCEPTED, status)
            answered++
        }
        assertEquals(3, answered)
        assertEquals(listOf(55, 56, 57), ordinals)
        assertEquals(listOf(
            listOf("findSavedFlows", "find", "Download my document"),
            listOf("getSavedFlowReview", "review", "flow.saved", 2),
            listOf("openSavedFlowStart", "open", "flow.saved", 2),
        ), calls)
        proxy.close()
    }

    @Test
    fun projectionRefusesWrongRequestPartialReviewAndContentOnARefusal() {
        assertNull(projectSavedFlowQuery(reply("old"), "new"))
        assertNull(projectSavedFlowQuery(reply("request").apply { flows[0].stepCount = 2 }, "request"))
        assertNull(projectSavedFlowQuery(reply("request").apply { availability = SavedFlowQueryAvailability.PRIVATE_PROFILE }, "request"))
        assertNull(projectSavedFlowQuery(reply("request").apply { availability = 255 }, "request"))
        val empty = reply("request").apply {
            availability = SavedFlowQueryAvailability.PRIVATE_PROFILE
            flows = emptyArray()
        }
        assertNotNull(empty.serialize())
        assertEquals(taffy.core_api.SavedFlowQueryAvailability.PRIVATE_PROFILE,
            projectSavedFlowQuery(empty, "request")?.availability)
    }

    private fun loopback(calls: MutableList<List<Any?>>, ordinals: MutableList<Int>): TaffyProfileCoreApi.Proxy {
        val implementation = Proxy.newProxyInstance(
            TaffyProfileCoreApi::class.java.classLoader,
            arrayOf(TaffyProfileCoreApi::class.java),
        ) { _, method, supplied ->
            val args = supplied.orEmpty()
            if (method.name in setOf("findSavedFlows", "getSavedFlowReview", "openSavedFlowStart")) {
                calls += listOf(method.name) + args.dropLast(1)
                val response = method.parameterTypes.last().methods.single { it.name == "call" }
                response.invoke(args.last(), if (method.name == "openSavedFlowStart") {
                    CoreApiSubmissionStatus.ACCEPTED
                } else reply(args[0] as String))
            }
            null
        } as TaffyProfileCoreApi
        val manager = TaffyProfileCoreApi.MANAGER
        // The generated factory hooks are protected. Reflection exposes only
        // these hooks; the generated request and response codecs run intact.
        val stubFactory = manager.javaClass.declaredMethods.single { it.name == "buildStub" && !it.isBridge }
        stubFactory.isAccessible = true
        val stub = stubFactory.invoke(manager, null, implementation, 0) as MessageReceiverWithResponder
        val receiver = object : MessageReceiverWithResponder {
            override fun accept(message: Message): Boolean = stub.accept(message)
            override fun acceptWithResponder(message: Message, responder: MessageReceiver): Boolean {
                ordinals += message.asServiceMessage().header.methodId
                return stub.acceptWithResponder(message, responder)
            }
            override fun close() = Unit
        }
        val proxyFactory = manager.javaClass.declaredMethods.single { it.name == "buildProxy" && !it.isBridge }
        proxyFactory.isAccessible = true
        return proxyFactory.invoke(manager, null, receiver) as TaffyProfileCoreApi.Proxy
    }

    private fun reply(requestId: String) = SavedFlowQueryResult().apply {
        this.requestId = requestId
        serviceGeneration = 7
        availability = SavedFlowQueryAvailability.AVAILABLE
        flows = arrayOf(SiteSkillView().apply {
            skillId = "flow.saved"
            origin = "https://example.test"
            provenance = SiteSkillProvenanceView.RECORDED_FROM_TASK
            status = SiteSkillStatusView.ACTIVE
            activeVersion = 2
            stepCount = 1
            recordedFromTaskId = "completed-task"
            reviewedSteps = arrayOf(SiteSkillObservedStep().apply {
                verb = "browser.navigate"
                arguments = arrayOf(SiteSkillObservedArgument().apply {
                    kind = SiteSkillArgumentKind.PUBLIC_ADDRESS
                    publicAddress = "https://example.test/start"
                })
            })
        })
    }
}

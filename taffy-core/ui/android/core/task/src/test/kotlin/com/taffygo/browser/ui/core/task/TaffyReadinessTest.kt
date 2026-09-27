// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.task.TaffyReadiness.NotSetUp
import com.taffygo.browser.ui.core.task.TaffyReadiness.Ready
import com.taffygo.browser.ui.core.task.TaffyReadiness.RouteChosenButNothingBehindIt
import com.taffygo.browser.ui.core.task.TaffyReadiness.Unknown
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The readiness rule, one test per row of its table plus the edges that bit. */
class TaffyReadinessTest {
    private val nothing = ProviderReadinessFacts(known = true)
    private val usableKey = nothing.copy(
        usableCredentialProviderIds = setOf("openai"),
        storedCredentialProviderIds = setOf("openai"),
    )
    private val pendingKey = nothing.copy(storedCredentialProviderIds = setOf("openai"))
    private val heldOnly = nothing.copy(heldCredentialProviderIds = setOf("openai"))
    private val ownAddress = nothing.copy(ownEndpointProviderIds = setOf("my-server"))
    private val direct = ProviderRoute.DIRECT_WITH_YOUR_KEY
    private val unnamed = listOf(ProviderRoute.NOT_CONFIGURED, ProviderRoute.NO_MODEL_REQUIRED)

    @Test
    fun `an incomplete projection is unknown whatever was chosen`() {
        for (route in ProviderRoute.entries) {
            assertEquals(Unknown, taffyReadiness(ProviderReadinessFacts.UNKNOWN, route))
            assertEquals(Unknown, taffyReadiness(usableKey.copy(known = false), route))
        }
    }

    @Test
    fun `your key is ready when a usable key exists`() {
        assertEquals(Ready(direct), taffyReadiness(usableKey, direct))
    }

    @Test
    fun `a usable key wins over a route that names nothing`() {
        for (route in unnamed) {
            assertEquals(Ready(direct), taffyReadiness(usableKey, route))
        }
    }

    @Test
    fun `nothing at all is not set up`() {
        for (route in unnamed) assertEquals(NotSetUp, taffyReadiness(nothing, route))
    }

    @Test
    fun `your key with no usable key names the route`() {
        val expected = RouteChosenButNothingBehindIt(direct)
        assertEquals(expected, taffyReadiness(nothing, direct))
        assertEquals(expected, taffyReadiness(pendingKey, direct))
        assertEquals(expected, taffyReadiness(heldOnly, direct))
    }

    @Test
    fun `a key that exists but is not ready names the direct route`() {
        for (route in unnamed) {
            assertEquals(RouteChosenButNothingBehindIt(direct), taffyReadiness(pendingKey, route))
            assertEquals(RouteChosenButNothingBehindIt(direct), taffyReadiness(heldOnly, route))
        }
    }

    @Test
    fun `an own address needs no key`() {
        assertEquals(Ready(direct), taffyReadiness(ownAddress, direct))
        for (route in unnamed) assertEquals(Ready(direct), taffyReadiness(ownAddress, route))
    }

    @Test
    fun `the verdict says whether set-up is needed and which route a request takes`() {
        assertFalse(Unknown.needsSetup)
        assertTrue(NotSetUp.needsSetup)
        assertTrue(RouteChosenButNothingBehindIt(direct).needsSetup)
        assertFalse(Ready(direct).needsSetup)
        assertEquals(ProviderRoute.NOT_CONFIGURED, Unknown.routeOrNone)
        assertEquals(ProviderRoute.NOT_CONFIGURED, NotSetUp.routeOrNone)
        // A route other than `direct`, so that `Ready(x).routeOrNone == x` is
        // not satisfied by every assertion here naming the same value.
        val other = ProviderRoute.NO_MODEL_REQUIRED
        assertEquals(ProviderRoute.NOT_CONFIGURED, RouteChosenButNothingBehindIt(other).routeOrNone)
        assertEquals(other, Ready(other).routeOrNone)
    }

    @Test
    fun `the providers that could answer are a usable key or an address of the person's own`() {
        val facts = usableKey.copy(
            storedCredentialProviderIds = setOf("openai", "anthropic"),
            ownEndpointProviderIds = setOf("my-server"),
            heldCredentialProviderIds = setOf("gemini"),
        )

        assertEquals(setOf("openai", "my-server"), facts.answeringProviderIds)
        assertEquals(emptySet<String>(), pendingKey.answeringProviderIds)
        assertEquals(emptySet<String>(), heldOnly.answeringProviderIds)
    }
}

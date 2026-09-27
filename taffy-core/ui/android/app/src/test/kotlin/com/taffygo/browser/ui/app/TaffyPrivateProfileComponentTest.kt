// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import dagger.Subcomponent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TaffyPrivateProfileComponentTest {
    @Test
    fun `private graph exposes only its lifetime and movable Tab builder`() {
        val component = TaffyPrivateProfileComponent::class.java
        val exposed = component.methods
            .filterNot { it.declaringClass == Any::class.java }
            .mapTo(sortedSetOf()) { it.name }

        assertEquals(sortedSetOf("lifetime", "tabBuilder"), exposed)
        assertFalse(exposed.contains("windowBuilder"))
        assertEquals(
            sortedSetOf("build", "identity"),
            TaffyPrivateProfileComponent.Builder::class.java.declaredMethods
                .mapTo(sortedSetOf()) { it.name },
        )
    }

    @Test
    fun `private graph installs no regular profile modules`() {
        val subcomponent = requireNotNull(
            TaffyPrivateProfileComponent::class.java.getAnnotation(Subcomponent::class.java),
        )

        assertTrue(subcomponent.modules.isEmpty())
    }

    @Test
    fun `process graph publishes distinct regular and private builders`() {
        val methods = TaffyProcessComponent::class.java.declaredMethods.associateBy { it.name }

        assertEquals(
            setOf("dispatchers", "lifetime", "privateProfileBuilder", "regularProfileBuilder"),
            methods.keys,
        )
        assertEquals(
            TaffyPrivateProfileComponent.Builder::class.java,
            methods.getValue("privateProfileBuilder").returnType,
        )
        assertEquals(
            TaffyProfileComponent.Builder::class.java,
            methods.getValue("regularProfileBuilder").returnType,
        )
    }
}

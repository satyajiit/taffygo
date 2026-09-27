// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The role vocabulary, held to the isolated core's.
 *
 * The Rust model router carries four values. The component-manifest and
 * contract gates own the cross-language graph; this local test keeps the UI
 * projection's count and wire labels explicit at the screen boundary.
 */
class ModelRoleTest {

    @Test
    fun `the four roles are the isolated core's four, spelled the isolated core's way`() {
        assertEquals(4, ModelRole.entries.size)
        assertEquals(
            listOf("primary_reasoning", "fast_browsing", "vision", "embedding"),
            ModelRole.entries.map { it.label },
        )
    }

    @Test
    fun `every label is unique, because a label reaches an audit record`() {
        val labels = ModelRole.entries.map { it.label }

        assertEquals(labels.size, labels.toSet().size)
    }

    @Test
    fun `a label is the constant name in lower snake case and nothing else`() {
        ModelRole.entries.forEach { role ->
            assertEquals(role.name.lowercase(), role.label)
        }
    }
}

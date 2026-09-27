// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The compiled flow map against the two documents that have to agree with it.
 *
 * A subscription sign-in is spelled out in three places, each owning one facet:
 * the core's `SIGN_IN_VENDORS` decides whether a roster row may be offered at
 * all, `ProviderSignInFlows` carries the flow's shape, and the browser's
 * pinned configuration carries the origins and the client identity. All three are hand-kept. When they drift, nothing fails
 * until somebody presses the button — the row offers a sign-in the core will
 * not admit, or the catalog offers one and the row says the version does not
 * carry it — and both are silent everywhere else.
 *
 * So the disagreement is made a failing test rather than a support ticket. Two
 * things are checked, from documents on disk rather than from anything this
 * module compiled:
 *
 * 1. the core's vendor list and this table name exactly the same vendors, and
 * 2. every provider the compiled catalog offers `OAUTH` for has a flow here.
 *
 * The browser's own pinned table is C++ and out of this module's reach, and
 * this test says nothing about it. It is not unchecked: `browserWillStart` on
 * every row here is the browser's own registration answer, and
 * `check_vendor_agreement.py` — the `catalog` lane of `./tools/check fast` —
 * reads `kVendors` and fails when the two disagree in either direction.
 */
class ProviderSignInFlowsParityTest {

    @Test
    fun `the core's vendor list and the compiled flow map name the same vendors`() {
        val kotlinVendors = ProviderSignInFlows.entries.map { it.providerId }.toSortedSet()
        val coreVendors = signInVendorsFromCore()

        assertEquals(
            "the core's SIGN_IN_VENDORS and ProviderSignInFlows have drifted; " +
                "adding a vendor is a change to both, plus the browser's pinned table",
            coreVendors,
            kotlinVendors,
        )
    }

    @Test
    fun `every provider the catalog offers a plan sign-in for has a compiled flow`() {
        val offering = oauthProvidersFromCatalog()

        // The other direction is deliberately not asserted. `ProviderSignInFlows`
        // says a vendor may be carried here ahead of its catalog row, so a flow
        // with no OAUTH row yet is the intended state rather than a defect; a
        // catalog row with no flow is not, because it renders as "sign-in not in
        // this version" for a vendor the product means to support.
        val missing = offering.filter { ProviderSignInFlows.of(it) == null }

        assertEquals(
            "the catalog offers a plan sign-in for these and this binary compiles none",
            emptyList<String>(),
            missing,
        )
    }

    @Test
    fun `the two documents this test reads are actually there`() {
        // The honesty half: both readers below return an empty set for a file
        // that does not parse the way this test expects, and an empty set
        // passes every assertion above. A missing or moved document has to
        // fail here rather than turn the parity check into a no-op.
        assertTrue(signInVendorsFromCore().isNotEmpty())
        assertTrue(oauthProvidersFromCatalog().isNotEmpty())
    }

    /** The vendor identifiers in the core's `SIGN_IN_VENDORS` array. */
    private fun signInVendorsFromCore(): Set<String> {
        val text = repositoryFile(CORE_CATALOG_PATH).readText()
        val declaration = SIGN_IN_VENDORS.find(text) ?: return emptySet()
        return QUOTED.findAll(declaration.groupValues[1])
            .map { it.groupValues[1] }
            .toSortedSet()
    }

    /** Every provider the compiled catalog snapshot offers `OAUTH` for. */
    private fun oauthProvidersFromCatalog(): List<String> {
        val text = repositoryFile(BASELINE_CATALOG_PATH).readText()
        val providers = text.substringAfter(PROVIDERS_KEY, "")
        // A brace-depth walk rather than a parser: the unit-test classpath
        // carries no JSON library, Android's own is a stub that answers null
        // here, and adding a dependency to read one generated document would
        // put it in front of Chromium's vendoring review for a test.
        return objectsIn(providers).mapNotNull { entry ->
            val id = PROVIDER_ID.find(entry)?.groupValues?.get(1) ?: return@mapNotNull null
            val methods = AUTH_METHODS.find(entry)?.groupValues?.get(1).orEmpty()
            id.takeIf { OAUTH_MEMBER.containsMatchIn(methods) }
        }
    }

    /** Each top-level `{ … }` object of one JSON array, as text. */
    private fun objectsIn(array: String): List<String> {
        val objects = mutableListOf<String>()
        var depth = 0
        var start = -1
        for ((index, character) in array.withIndex()) {
            when (character) {
                '{' -> {
                    if (depth == 0) start = index
                    depth++
                }

                '}' -> {
                    depth--
                    if (depth == 0 && start >= 0) objects += array.substring(start, index + 1)
                    if (depth < 0) return objects
                }

                ']' -> if (depth == 0) return objects
            }
        }
        return objects
    }

    /**
     * One repository file, found by walking up from wherever the test runs.
     *
     * The working directory of a Gradle test task is not a promise, so the root
     * is located by a marker that only the repository root has. Failing loudly
     * is the point: a test that silently could not find its inputs is a test
     * that reports success for something it never read.
     */
    private fun repositoryFile(relativePath: String): File {
        var directory: File? = File(System.getProperty("user.dir").orEmpty()).absoluteFile
        while (directory != null) {
            if (File(directory, ROOT_MARKER).isFile) {
                val found = File(directory, relativePath)
                check(found.isFile) { "$relativePath is missing from the repository" }
                return found
            }
            directory = directory.parentFile
        }
        error("no repository root above ${System.getProperty("user.dir")}")
    }

    private companion object {
        const val ROOT_MARKER = "taffy-core/build/components.toml"
        const val CORE_CATALOG_PATH =
            "taffy-core/components/intelligence/core/rust/core-runtime/src/composition/" +
                "provider_catalog.rs"
        const val BASELINE_CATALOG_PATH =
            "taffy-core/components/intelligence/core/rust/model-router/catalog/baseline.json"
        const val PROVIDERS_KEY = "\"providers\""

        val SIGN_IN_VENDORS = Regex("""SIGN_IN_VENDORS\s*:\s*\[[^]]*]\s*=\s*\[([^]]*)]""")
        val QUOTED = Regex("\"([^\"]+)\"")
        val PROVIDER_ID = Regex("\"provider_id\"\\s*:\\s*\"([^\"]+)\"")
        val AUTH_METHODS = Regex("\"auth_methods\"\\s*:\\s*\\[([^]]*)]")
        val OAUTH_MEMBER = Regex("\"OAUTH\"")
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

/**
 * The compiled flow map: every vendor this binary can sign in to.
 *
 * Compiled and not served, on purpose. A flow names pinned origins and a
 * shape — binary behaviour a served catalog must not be able to invent or
 * move. The catalog's part is enablement only: a
 * sign-in starts when the vendor is here *and* its roster row offers OAUTH
 * and ships switched on, so a vendor whose terms review has not happened is
 * carried here with no way to start it, and a catalog update lights it up
 * without a release.
 */
object ProviderSignInFlows {

    /**
     * Every flow this binary carries, in roster order.
     *
     * `browserWillStart` mirrors `ProviderAuthVendorRegistered`'s answer for
     * each row of the browser's `kVendors`, and only the owner moves it: it is
     * false wherever the terms review is undated, and dating a review is an
     * owner's act (decision 0081). `check_vendor_agreement.py` fails when a
     * value here disagrees with the browser's table, so the two are edited
     * together or not at all.
     *
     * Every row is true today, which is the finished state of this column and
     * not the absence of one. It stays in the map because the next vendor
     * added arrives undated like every one of these did, and because a review
     * can be withdrawn: what the column carries is the browser's answer, and
     * a surface that assumed the answer was always yes would draw a sign-in
     * that refuses itself.
     */
    val entries: List<ProviderSignInFlow> = listOf(
        ProviderSignInFlow(
            "anthropic",
            ProviderFlowKind.PKCE,
            browserWillStart = true,
        ),
        ProviderSignInFlow(
            "github-copilot",
            ProviderFlowKind.DEVICE_CODE,
            browserWillStart = true,
        ),
        ProviderSignInFlow(
            "kimi-coding",
            ProviderFlowKind.DEVICE_CODE,
            browserWillStart = true,
        ),
        ProviderSignInFlow(
            "openai",
            ProviderFlowKind.PKCE,
            browserWillStart = true,
        ),
        ProviderSignInFlow(
            "openrouter",
            ProviderFlowKind.PKCE,
            browserWillStart = true,
        ),
        ProviderSignInFlow(
            "xai",
            ProviderFlowKind.DEVICE_CODE,
            browserWillStart = true,
        ),
    )

    /**
     * Every compiled vendor, mapped to whether the browser will start it.
     *
     * The shape every surface that decides what to *draw* wants, and it is
     * here rather than derived at five call sites: each projection built its
     * own set from [entries], which was five copies of one derivation and
     * five places for the next fact about a flow to be left out of. Absent
     * means this binary carries no flow at all; false means it carries one the
     * browser refuses to begin.
     */
    val byVendor: Map<String, Boolean> =
        entries.associate { it.providerId to it.browserWillStart }

    /**
     * Every compiled vendor whose flow returns through a redirect.
     *
     * The set the manual-code fallback of decision 0095 section 2 applies to:
     * a redirect can fail to come back, so a screen waiting on one offers a
     * place to paste what the vendor showed. A device-code flow has no
     * redirect and never shows the field. Derived from [entries] rather than
     * kept as a second list, so the shape column and this set cannot
     * disagree — and the browser stays the authority either way, refusing a
     * submission for any flow that is not PKCE.
     */
    val pkceVendors: Set<String> =
        entries.filter { it.kind == ProviderFlowKind.PKCE }.map { it.providerId }.toSet()

    /** The flow for one vendor, or `null` when this binary has none. */
    fun of(providerId: String): ProviderSignInFlow? =
        entries.firstOrNull { it.providerId == providerId }
}

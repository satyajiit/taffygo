// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** Who the page is about: the vendor's mark, their name, and what they take. */
@Composable
internal fun ProviderConfigHeader(state: ProviderConfigUiState) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(bottom = TaffyTheme.spacing.step),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ProviderBadge(
            providerId = state.providerId,
            name = state.displayName,
            size = HeaderBadgeSize,
        )
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = state.displayName,
                style = TaffyTheme.typography.headline,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = taffyString(headerLeadRes(state)),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

/**
 * The card over a credential that is already stored.
 *
 * Three actions, in the order a person needs them: change the model, sign in
 * again, and — last and in the danger treatment — remove it. Sign in again is
 * absent for a pasted key with no compiled flow behind it, because there is
 * nothing to sign in to and a button that cannot work is worse than none.
 */
@Composable
internal fun ManagedCredentialCard(
    managed: ManagedCredential,
    state: ProviderConfigUiState,
    onIntent: (ProviderConfigIntent) -> Unit,
) {
    NoticeCard(
        icon = if (managed.confirmed) TaffyIcon.CheckCircle else TaffyIcon.Warning,
        tint = if (managed.confirmed) TaffyTheme.colors.positive else TaffyTheme.colors.caution,
        border = if (managed.confirmed) TaffyTheme.colors.outline else TaffyTheme.colors.caution,
        title = taffyString(
            if (managed.confirmed) {
                R.string.taffy_providers_managed_title
            } else {
                R.string.taffy_providers_managed_unconfirmed
            },
        ),
        body = managedBody(managed),
        testTag = PROVIDER_MANAGED_TEST_TAG,
    ) {
        Text(
            text = taffyString(
                R.string.taffy_providers_managed_model,
                managed.modelName
                    ?: taffyString(R.string.taffy_providers_managed_model_provider_order),
            ),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        managed.lastRefusal?.let { refusal ->
            ProviderRefusalLine(
                refusal = refusal,
                displayName = state.displayName,
                testTag = PROVIDER_MANAGED_REFUSAL_TEST_TAG,
            )
        }
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_providers_managed_change_model),
            onClick = { onIntent(ProviderConfigIntent.ChangeModel) },
            enabled = !state.signingOut,
            icon = TaffyIcon.SlidersHorizontal,
            size = TaffyButtonSize.COMPACT,
            modifier = Modifier.fillMaxWidth(),
            testTag = PROVIDER_CHANGE_MODEL_TEST_TAG,
        )
        if (managed.canReauthenticate) {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_providers_managed_reauthenticate),
                onClick = { onIntent(ProviderConfigIntent.StartSignIn) },
                enabled = !state.signingOut,
                icon = TaffyIcon.ArrowClockwise,
                size = TaffyButtonSize.COMPACT,
                modifier = Modifier.fillMaxWidth(),
                testTag = PROVIDER_REAUTH_TEST_TAG,
            )
        }
        TaffyDangerButton(
            label = taffyString(
                if (state.signingOut) {
                    R.string.taffy_providers_managed_signing_out
                } else {
                    R.string.taffy_providers_managed_sign_out
                },
            ),
            onClick = { onIntent(ProviderConfigIntent.AskSignOut) },
            enabled = !state.signingOut,
            loading = state.signingOut,
            size = TaffyButtonSize.COMPACT,
            modifier = Modifier.fillMaxWidth(),
            testTag = PROVIDER_SIGN_OUT_TEST_TAG,
        )
    }
}

/** The plan road: one sentence and one action, above the rule. */
@Composable
internal fun SignInBlock(
    state: ProviderConfigUiState,
    onIntent: (ProviderConfigIntent) -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth().testTag(PROVIDER_SIGN_IN_BLOCK_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = taffyString(R.string.taffy_providers_signin_blurb),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffyPrimaryButton(
            label = taffyString(R.string.taffy_providers_signin_cta, state.displayName),
            onClick = { onIntent(ProviderConfigIntent.StartSignIn) },
            icon = TaffyIcon.ShieldCheck,
            modifier = Modifier.fillMaxWidth(),
            testTag = PROVIDER_SIGN_IN_CTA_TEST_TAG,
        )
    }
}

/**
 * The catalog offers a plan sign-in this binary does not compile.
 *
 * Explained where the button would have been, and nothing else changes: the key
 * form below is untouched if the provider has one, and absent if it does not.
 * Degrading a sign-in into a key field would offer a credential the vendor does
 * not issue.
 */
@Composable
internal fun SignInNotBuiltNotice(displayName: String) {
    NoticeCard(
        icon = TaffyIcon.Info,
        tint = TaffyTheme.colors.textSecondary,
        border = TaffyTheme.colors.outline,
        title = taffyString(R.string.taffy_providers_signin_not_built_title),
        body = taffyString(R.string.taffy_providers_signin_not_built_body, displayName),
        testTag = PROVIDER_SIGN_IN_UNAVAILABLE_TEST_TAG,
    )
}

/**
 * The vendor's sign-in is in this version and is not permitted to start.
 *
 * The same card and the opposite sentence to the one above it: saying "not in
 * this version" here would send somebody looking for an update that already
 * contains what they are waiting for.
 */
@Composable
internal fun SignInNotClearedNotice(displayName: String) {
    NoticeCard(
        icon = TaffyIcon.Info,
        tint = TaffyTheme.colors.textSecondary,
        border = TaffyTheme.colors.outline,
        title = taffyString(R.string.taffy_providers_signin_not_cleared_title),
        body = taffyString(R.string.taffy_providers_signin_not_cleared_body, displayName),
        testTag = PROVIDER_SIGN_IN_UNAVAILABLE_TEST_TAG,
    )
}

/** A reason the product has already given, stated in full on the page. */
@Composable
internal fun ProviderBlockedNotice(reason: ProviderRowOffer.Reason) {
    NoticeCard(
        icon = TaffyIcon.Prohibit,
        tint = TaffyTheme.colors.danger,
        border = TaffyTheme.colors.danger,
        title = taffyString(R.string.taffy_providers_config_blocked_title),
        body = taffyString(blockedBodyRes(reason)),
        testTag = PROVIDER_CONFIG_BLOCKED_TEST_TAG,
    )
}

/**
 * Where Taffy's own requests go, and what choosing this provider would mean.
 *
 * Four answers rather than a switch, because two of them are things the product
 * cannot do: nothing is connected, or several things are and nothing in the
 * product names which one a request uses. Those two draw a sentence and no
 * control at all.
 */
@Composable
internal fun ProviderDefaultSection(
    state: ProviderConfigUiState,
    onIntent: (ProviderConfigIntent) -> Unit,
) {
    if (state.defaultChoice == ProviderDefaultChoice.UNAVAILABLE) return
    Column(
        modifier = Modifier.fillMaxWidth().testTag(PROVIDER_DEFAULT_SECTION_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = taffyString(R.string.taffy_providers_default_title),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
        )
        when (state.defaultChoice) {
            ProviderDefaultChoice.OFFERED -> TaffySecondaryButton(
                label = taffyString(R.string.taffy_providers_default_cta),
                onClick = { onIntent(ProviderConfigIntent.UseForTaffy) },
                icon = TaffyIcon.Sparkle,
                modifier = Modifier.fillMaxWidth(),
                testTag = PROVIDER_DEFAULT_CTA_TEST_TAG,
            )

            ProviderDefaultChoice.IN_FORCE -> Text(
                text = taffyString(R.string.taffy_providers_default_in_force),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
            )

            ProviderDefaultChoice.SHARED -> Text(
                text = taffyString(R.string.taffy_providers_default_shared),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
            )

            ProviderDefaultChoice.UNAVAILABLE -> Unit
        }
        Text(
            text = taffyString(R.string.taffy_providers_default_note),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/** One raised card with a glyph, a heading, a line of prose and whatever else. */
@Composable
private fun NoticeCard(
    icon: ImageVector,
    tint: Color,
    border: Color,
    title: String,
    body: String,
    testTag: String,
    content: @Composable () -> Unit = {},
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, border, TaffyTheme.shapes.card)
            .padding(TaffyTheme.spacing.cardPadding)
            .testTag(testTag),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Row(
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = tint,
                modifier = Modifier.size(NoticeGlyphSize),
            )
            Text(
                text = title,
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.weight(1f),
            )
        }
        Text(
            text = body,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        content()
    }
}

/**
 * What the vendor said about this credential, or what stands behind it.
 *
 * The vendor's own words come first, because which of a person's accounts this
 * is and on what plan is the one thing they could not have worked out. With
 * nothing said, the card describes the kind of credential instead.
 */
@Composable
private fun managedBody(managed: ManagedCredential): String {
    if (!managed.confirmed) {
        return taffyString(R.string.taffy_providers_managed_unconfirmed_body)
    }
    val account = managed.accountLabel
    val plan = managed.planLabel
    return when {
        account != null && plan != null ->
            taffyString(R.string.taffy_providers_label_pair, account, plan)

        account != null -> account
        plan != null -> plan
        managed.subscriptionBacked -> taffyString(R.string.taffy_providers_managed_body_plan)
        else -> taffyString(R.string.taffy_providers_managed_body_key)
    }
}

private fun headerLeadRes(state: ProviderConfigUiState): Int = when {
    state.blocked != null -> R.string.taffy_providers_config_lead_blocked
    state.signIn != ProviderSignInOffer.NONE && state.keyForm != null ->
        R.string.taffy_providers_config_lead_both

    state.keyForm != null -> R.string.taffy_providers_config_lead_key
    state.signIn != ProviderSignInOffer.NONE -> R.string.taffy_providers_config_lead_plan
    else -> R.string.taffy_providers_config_lead_blocked
}

private fun blockedBodyRes(reason: ProviderRowOffer.Reason): Int = when (reason) {
    ProviderRowOffer.Reason.NOT_ACTIONABLE ->
        R.string.taffy_providers_config_block_not_actionable

    ProviderRowOffer.Reason.HELD_SHUT -> R.string.taffy_providers_config_block_held_shut
    ProviderRowOffer.Reason.NO_METHOD -> R.string.taffy_providers_config_block_no_method
    // The page never reaches this: a vendor whose sign-in this build lacks is
    // explained by the sign-in block, which can say which vendor it is, and
    // `ProviderConfigProjection.refusalFor` therefore never returns it. The
    // sentence chosen if it ever did is the true one for that case — this
    // version cannot, a later one may — rather than an arm that would have to
    // be noticed to be wrong.
    ProviderRowOffer.Reason.SIGN_IN_NOT_BUILT ->
        R.string.taffy_providers_config_block_not_actionable

    // Nor this one, and for the same reason. Its sentence is the honest one
    // for a flow that is built and not cleared to run: nothing about the
    // phone, and nothing the person can do.
    ProviderRowOffer.Reason.SIGN_IN_NOT_CLEARED ->
        R.string.taffy_providers_config_block_not_cleared
}

/** The tags screen SCR-415's semantics tests name. */
const val PROVIDER_MANAGED_TEST_TAG: String = "providers_managed"
const val PROVIDER_MANAGED_REFUSAL_TEST_TAG: String = "providers_managed_refusal"
const val PROVIDER_CHANGE_MODEL_TEST_TAG: String = "providers_change_model"
const val PROVIDER_REAUTH_TEST_TAG: String = "providers_reauthenticate"
const val PROVIDER_SIGN_OUT_TEST_TAG: String = "providers_sign_out"
const val PROVIDER_SIGN_IN_BLOCK_TEST_TAG: String = "providers_sign_in_block"
const val PROVIDER_SIGN_IN_CTA_TEST_TAG: String = "providers_sign_in_cta"
const val PROVIDER_SIGN_IN_UNAVAILABLE_TEST_TAG: String = "providers_sign_in_unavailable"
const val PROVIDER_CONFIG_BLOCKED_TEST_TAG: String = "providers_config_blocked"
const val PROVIDER_DEFAULT_SECTION_TEST_TAG: String = "providers_default_section"
const val PROVIDER_DEFAULT_CTA_TEST_TAG: String = "providers_default_cta"

private val HeaderBadgeSize = 48.dp
private val NoticeGlyphSize = 20.dp

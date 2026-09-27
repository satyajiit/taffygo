// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.hardware.biometrics.BiometricManager
import android.hardware.biometrics.BiometricPrompt
import android.os.Build
import android.os.CancellationSignal
import android.view.View
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalView

/**
 * System device lock or biometrics. We never draw a PIN pad.
 *
 * Safe with no Activity (previews, tests): [onResult] is false.
 */
internal fun requestDeviceCredential(
    context: Context,
    view: View,
    title: String,
    onResult: (Boolean) -> Unit,
): () -> Unit {
    if (view.isInEditMode) {
        onResult(false)
        return {}
    }
    val activity = context.findActivity()
    if (activity == null) {
        onResult(false)
        return {}
    }
    val cancel = CancellationSignal()
    val callback = object : BiometricPrompt.AuthenticationCallback() {
        override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
            onResult(true)
        }

        override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
            onResult(false)
        }
    }
    val builder = BiometricPrompt.Builder(activity).setTitle(title)
    if (Build.VERSION.SDK_INT >= 30) {
        builder.setAllowedAuthenticators(
            BiometricManager.Authenticators.BIOMETRIC_WEAK or
                BiometricManager.Authenticators.DEVICE_CREDENTIAL,
        )
    } else {
        @Suppress("DEPRECATION")
        builder.setDeviceCredentialAllowed(true)
    }
    builder.build().authenticate(cancel, activity.mainExecutor, callback)
    return { cancel.cancel() }
}

@Composable
internal fun currentComposeView(): View = LocalView.current

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}

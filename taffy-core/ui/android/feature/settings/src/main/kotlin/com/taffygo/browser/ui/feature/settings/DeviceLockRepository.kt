// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import android.app.KeyguardManager
import android.content.Context

/** Whether this phone can gate a secret behind a screen lock. */
interface DeviceLockRepository {

    /** True when the person has set a lock the system will honour. */
    fun hasScreenLock(): Boolean
}

/** [KeyguardManager.isDeviceSecure] on the application context. */
internal class AndroidDeviceLockRepository(
    context: Context,
) : DeviceLockRepository {
    private val keyguard = context.applicationContext.getSystemService(KeyguardManager::class.java)

    override fun hasScreenLock(): Boolean = keyguard?.isDeviceSecure == true
}

/** Tests that need a phone with no lock. */
internal class UnlockedDeviceLockRepository : DeviceLockRepository {
    override fun hasScreenLock(): Boolean = false
}

/** Tests that need a phone that can reveal. */
internal class LockedDeviceLockRepository : DeviceLockRepository {
    override fun hasScreenLock(): Boolean = true
}

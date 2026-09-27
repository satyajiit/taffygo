// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.mojo.system.MessagePipeHandle;
import org.chromium.mojo.system.impl.CoreImpl;
import org.chromium.taffy.browser.field_values.mojom.TaffyFieldValueSurface;

/** Connects Android's profile-owned form sheet directly to the browser vault. */
public final class TaffyFieldValueBridge {
    /** Returns the value-only surface; no bytes sent here can enter the Core API. */
    public static TaffyFieldValueSurface connect(Profile profile) {
        long nativeHandle = TaffyFieldValueBridgeJni.get().connect(profile);
        if (nativeHandle == 0) {
            throw new IllegalStateException("The profile field-value pipe is unavailable");
        }
        MessagePipeHandle handle =
                CoreImpl.getInstance().acquireNativeHandle(nativeHandle).toMessagePipeHandle();
        return TaffyFieldValueSurface.MANAGER.attachProxy(handle, 0);
    }

    private TaffyFieldValueBridge() {}

    @NativeMethods
    interface Natives {
        long connect(@JniType("Profile*") Profile profile);
    }
}

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
import org.chromium.taffy.core_api.mojom.TaffyProfileCoreApi;

/** Creates the generated Mojo proxy for one browser-owned profile facade. */
public final class TaffyCoreApiBridge {
    /** The returned proxy owns the only native pipe handle transferred by JNI. */
    public static TaffyProfileCoreApi connect(Profile profile) {
        long nativeHandle = TaffyCoreApiBridgeJni.get().connect(profile);
        if (nativeHandle == 0) {
            throw new IllegalStateException("The profile Core API pipe is unavailable");
        }
        MessagePipeHandle handle =
                CoreImpl.getInstance().acquireNativeHandle(nativeHandle).toMessagePipeHandle();
        return TaffyProfileCoreApi.MANAGER.attachProxy(handle, 0);
    }

    private TaffyCoreApiBridge() {}

    @NativeMethods
    interface Natives {
        long connect(@JniType("Profile*") Profile profile);
    }
}

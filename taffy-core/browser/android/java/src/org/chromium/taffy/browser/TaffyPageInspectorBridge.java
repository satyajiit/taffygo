// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.content_public.browser.WebContents;
import org.chromium.mojo.system.MessagePipeHandle;
import org.chromium.mojo.system.impl.CoreImpl;
import org.chromium.taffy.core_api.mojom.TaffyPageInspector;

/** Connects one generated page-inspector proxy to one exact WebContents. */
public final class TaffyPageInspectorBridge {
    public static TaffyPageInspector connect(Profile profile, WebContents webContents) {
        long nativeHandle = TaffyPageInspectorBridgeJni.get().connect(profile, webContents);
        if (nativeHandle == 0) {
            throw new IllegalStateException("The selected-page pipe is unavailable");
        }
        MessagePipeHandle handle =
                CoreImpl.getInstance().acquireNativeHandle(nativeHandle).toMessagePipeHandle();
        return TaffyPageInspector.MANAGER.attachProxy(handle, 0);
    }

    private TaffyPageInspectorBridge() {}

    @NativeMethods
    interface Natives {
        long connect(
                @JniType("Profile*") Profile profile,
                @JniType("content::WebContents*") WebContents webContents);
    }
}

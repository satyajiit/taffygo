// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import org.chromium.chrome.browser.profiles.Profile;
import org.jni_zero.NativeMethods;
import org.chromium.mojo.bindings.InterfaceRequest;
import org.chromium.mojo.bindings.Router;
import org.chromium.mojo.system.Pair;
import org.chromium.mojo.system.impl.CoreImpl;
import org.chromium.taffy.browser.account.mojom.TaffyProfilePlatformAdapter;

/** Native backchannel for Android-only profile platform facts. */
public final class TaffyProfilePlatformBridge {
    private TaffyProfilePlatformBridge() {}

    /** The browser validates and correlates this bounded URI; Java never parses it. */
    public static boolean deliverAuthCallback(Profile profile, String rawUri) {
        return TaffyProfilePlatformBridgeJni.get().deliverAuthCallback(profile, rawUri);
    }

    /** Binds one profile-owned Java adapter to the browser account broker. */
    public static Router bindPlatformAdapter(
            Profile profile, TaffyProfilePlatformAdapter adapter) {
        Pair<TaffyProfilePlatformAdapter.Proxy, InterfaceRequest<TaffyProfilePlatformAdapter>> pipe =
                TaffyProfilePlatformAdapter.MANAGER.getInterfaceRequest(CoreImpl.getInstance());
        Router router = TaffyProfilePlatformAdapter.MANAGER.bind(adapter, pipe.second);
        long nativeHandle = pipe.first.getProxyHandler().passHandle().releaseNativeHandle();
        if (!TaffyProfilePlatformBridgeJni.get().bindPlatformAdapter(profile, nativeHandle)) {
            router.close();
            throw new IllegalStateException("The profile platform adapter could not be bound");
        }
        return router;
    }

    /**
     * Hands a signed-out subscription's token to the browser for vendor-side
     * revocation. Best-effort: the sealed record is already gone whatever the
     * vendor answers, so there is nothing to wait for and nothing comes back.
     */
    public static void revokeProviderCredential(Profile profile, String providerId, String token) {
        TaffyProfilePlatformBridgeJni.get().revokeProviderCredential(profile, providerId, token);
    }

    /**
     * The manual-code fallback: what a vendor displayed and the person typed
     * on trusted chrome, handed over whole. Some vendors show the code and the
     * state joined by a {@code #}; the browser owns that shape and every bound
     * on it, so Java neither splits nor inspects the value. False means no
     * live flow accepted it, which a screen shows as "that code did not work"
     * rather than as an error — the person can try again until the deadline.
     */
    public static boolean submitProviderAuthCode(Profile profile, String flowId, String code) {
        return TaffyProfilePlatformBridgeJni.get().submitProviderAuthCode(profile, flowId, code);
    }

    @NativeMethods
    interface Natives {
        boolean bindPlatformAdapter(Profile profile, long messagePipeHandle);

        boolean deliverAuthCallback(Profile profile, String rawUri);

        void revokeProviderCredential(Profile profile, String providerId, String token);

        boolean submitProviderAuthCode(Profile profile, String flowId, String code);
    }
}

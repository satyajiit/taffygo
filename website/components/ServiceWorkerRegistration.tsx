// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

"use client";

import { useEffect } from "react";
import { withBasePath } from "@/lib/base-path";

/** Production only: dev builds must never cache development bundles. */
export function ServiceWorkerRegistration() {
  useEffect(() => {
    if (process.env.NODE_ENV !== "production" || !("serviceWorker" in navigator)) return;
    let registration: ServiceWorkerRegistration | undefined;
    let disposed = false;
    let controlled = Boolean(navigator.serviceWorker.controller);
    let refreshing = false;
    const onControllerChange = () => {
      if (controlled && !refreshing) {
        refreshing = true;
        window.location.reload();
      }
      controlled = true;
    };
    const checkForUpdate = () => {
      if (navigator.onLine) void registration?.update().catch(() => {});
    };
    const onVisibilityChange = () => {
      if (document.visibilityState === "visible") checkForUpdate();
    };
    const register = () => {
      void navigator.serviceWorker.register(withBasePath("/sw.js"), {
        scope: withBasePath("/"),
        updateViaCache: "none",
      }).then((value) => {
        if (disposed) return;
        registration = value;
        checkForUpdate();
      }).catch(() => {}); // The static site remains usable when storage is denied.
    };
    navigator.serviceWorker.addEventListener("controllerchange", onControllerChange);
    window.addEventListener("online", checkForUpdate);
    document.addEventListener("visibilitychange", onVisibilityChange);
    if (document.readyState === "complete") register();
    else window.addEventListener("load", register, { once: true });
    const interval = window.setInterval(checkForUpdate, 30 * 60 * 1000);
    return () => {
      disposed = true;
      window.clearInterval(interval);
      window.removeEventListener("load", register);
      window.removeEventListener("online", checkForUpdate);
      document.removeEventListener("visibilitychange", onVisibilityChange);
      navigator.serviceWorker.removeEventListener("controllerchange", onControllerChange);
    };
  }, []);
  return null;
}

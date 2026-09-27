// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

"use client";

import { Moon, Sun } from "lucide-react";
import { useSyncExternalStore } from "react";
import { themeSwitch } from "@/lib/content";

type Theme = "light" | "dark";

const STORAGE_KEY = "taffygo-theme";

function applyTheme(next: Theme) {
  document.documentElement.dataset.taffyTheme = next;
  try {
    window.localStorage.setItem(STORAGE_KEY, next);
  } catch {
    // Storage can be unavailable (private mode); the choice still holds for
    // the session, it just is not persisted.
  }
}

function subscribe(onChange: () => void) {
  const observer = new MutationObserver(onChange);
  observer.observe(document.documentElement, {
    attributes: true,
    attributeFilter: ["data-taffy-theme"],
  });
  return () => observer.disconnect();
}

function snapshot(): Theme {
  return document.documentElement.dataset.taffyTheme === "dark" ? "dark" : "light";
}

/**
 * A two-button sun and moon control. The
 * inline script in app/layout.tsx has already set data-taffy-theme on
 * <html> before this hydrates. The attribute is read through
 * useSyncExternalStore: the server snapshot renders both halves unpressed
 * (the theme is unknown on the server), the client store re-reads the DOM
 * after hydration, and the observer keeps the control in sync, so there is
 * no flash and no hydration mismatch.
 */
export function ThemeSwitch() {
  const theme = useSyncExternalStore(subscribe, snapshot, () => null);

  const halfClass = (active: boolean) =>
    `grid h-9 w-9 place-items-center rounded-full transition-colors ${
      active
        ? "border-2 border-primary bg-sheet text-primary"
        : "border-2 border-transparent text-secondary hover:text-primary"
    }`;

  return (
    <div
      role="group"
      aria-label={themeSwitch.groupLabel}
      className="flex h-11 shrink-0 items-center gap-0.5 rounded-full border border-outline p-[3px]"
    >
      <button
        type="button"
        aria-pressed={theme === "light"}
        aria-label={themeSwitch.toLight}
        onClick={() => applyTheme("light")}
        className={halfClass(theme === "light")}
      >
        <Sun aria-hidden="true" size={17} strokeWidth={1.75} />
      </button>
      <button
        type="button"
        aria-pressed={theme === "dark"}
        aria-label={themeSwitch.toDark}
        onClick={() => applyTheme("dark")}
        className={halfClass(theme === "dark")}
      >
        <Moon aria-hidden="true" size={17} strokeWidth={1.75} />
      </button>
    </div>
  );
}

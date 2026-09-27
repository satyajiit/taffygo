// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { Callout, TourShot } from "./home";

/**
 * Copy for /built-for-phones/. Every claim is visible in the capture it sits
 * beside. The address bar is at the top of a page in the current app, so the
 * page says what is at the bottom and does not claim more.
 */

export type PhoneSection = {
  readonly id: string;
  readonly title: string;
  readonly body: string;
  readonly shots: ReadonlyArray<TourShot>;
};

export const phonesHero = {
  title: "See the Android app",
  lede:
    "Explore the bottom toolbar, separate task tabs, and light and dark themes in real app captures.",
} as const;

const bottomRow: ReadonlyArray<Callout> = [
  { text: "Back and forward.", x: 11, y: 95.5 },
  { text: "Ask Taffy, about this page or an errand.", x: 42, y: 95.5 },
  { text: "Your tabs, with the count.", x: 96, y: 92 },
];

const taffysTabs: ReadonlyArray<Callout> = [
  { text: "Your tabs, and the switch to private tabs.", x: 50, y: 5 },
  { text: "Taffy's tabs, grouped apart from yours.", x: 62, y: 65 },
  { text: "An amber edge marks a tab Taffy opened for a task.", x: 92, y: 70.5 },
];

export const phoneSections: ReadonlyArray<PhoneSection> = [
  {
    id: "bottom-row",
    title: "The bottom row",
    body:
      "Back, forward, Ask Taffy and your tabs sit in one row at the bottom " +
      "of every page. Ask Taffy opens as a panel over the page you're on.",
    shots: [{ screen: "05-asks-before-acting", callouts: bottomRow }],
  },
  {
    id: "taffys-tabs",
    title: "Taffy's tabs stay out of yours",
    body:
      "Tabs Taffy opens for an errand are grouped on their own in the tab " +
      "switcher, and each one has an amber edge. You can turn your open tabs " +
      "into a workspace from the same screen.",
    shots: [{ screen: "10-tabs", callouts: taffysTabs }],
  },
  {
    id: "light-or-dark",
    title: "Light or dark",
    body:
      "TaffyGo follows your phone's light or dark setting. To choose one for " +
      "TaffyGo alone, open Settings, then Appearance.",
    shots: [
      { screen: "11-start-page-dark", caption: "The start page in the dark theme." },
      { screen: "12-taffy-dark", caption: "Ask Taffy in the dark theme." },
    ],
  },
];

export const phonesRequirements = {
  title: "What it runs on",
  body: "Android 10 or later, on a phone with a 64-bit ARM processor.",
} as const;

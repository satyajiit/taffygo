// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/**
 * The real phone screens the site shows. Each one is a capture of the app on
 * an Android phone, taken with tools/screens/capture on 26 September 2026
 * and derived into public/screens/ by scripts/build_media.py. The status and
 * navigation bars are cropped off, so a screen starts at the app's own top
 * edge. Alt text says what is on the screen, in the app's own words.
 *
 * A product change that makes a screen here wrong needs a new capture, not
 * an edited description.
 */

export type ScreenId =
  | "01-start-page"
  | "02-taffy-on-a-page"
  | "03-ad-blocking"
  | "04-errand-running"
  | "05-asks-before-acting"
  | "06-results"
  | "07-your-provider-your-key"
  | "08-privacy"
  | "09-backup"
  | "10-tabs"
  | "11-start-page-dark"
  | "12-taffy-dark"
  | "x-ask-about-this-page";

export type Screen = {
  readonly id: ScreenId;
  readonly alt: string;
};

/** Pixel size of the two widths every screen is served at. */
export const SCREEN_SIZES = [
  { width: 360, height: 723 },
  { width: 720, height: 1447 },
] as const;

export const screens: Readonly<Record<ScreenId, Screen>> = {
  "01-start-page": {
    id: "01-start-page",
    alt:
      "TaffyGo's start page with a request typed into its one box. The app " +
      "offers it three ways: as a task for Taffy, as a search, or as a " +
      "question for Taffy.",
  },
  "02-taffy-on-a-page": {
    id: "02-taffy-on-a-page",
    alt:
      "Ask Taffy open over a recipe page. Taffy lists the ingredients and " +
      "times from Source 1, notes that the page gives no equipment list, " +
      "and marks the answer Partly done.",
  },
  "03-ad-blocking": {
    id: "03-ad-blocking",
    alt:
      "The page sheet for allrecipes.com: 20 ads and trackers blocked on " +
      "this page, and a switch that blocks ads and trackers on this site.",
  },
  "04-errand-running": {
    id: "04-errand-running",
    alt:
      "An errand running: Taffy compares a Kindle Paperwhite's price on " +
      "flipkart.com and amazon.in. The screen shows the two pages, what " +
      "Taffy is doing, and Take over, Pause and Stop.",
  },
  "05-asks-before-acting": {
    id: "05-asks-before-acting",
    alt:
      "The Ask Taffy panel with a request to find a Kindle Paperwhite's " +
      "price on croma.com. Above the box Taffy says it may open up to 8 " +
      "sites, that private details go only to the site, and that page " +
      "text goes directly to your provider.",
  },
  "06-results": {
    id: "06-results",
    alt:
      "The result of comparing a Kindle Paperwhite on flipkart.com and " +
      "amazon.in: the lowest listed offer, ₹16,999 at Flipkart, then each " +
      "listing's details with its source, marked Partly done.",
  },
  "07-your-provider-your-key": {
    id: "07-your-provider-your-key",
    alt:
      "The AI providers screen on its API keys tab: Anthropic, Baseten, " +
      "Cerebras, Chutes, DeepInfra, DeepSeek and Fireworks AI, each with " +
      "Set up, and Add your own provider at the bottom.",
  },
  "08-privacy": {
    id: "08-privacy",
    alt:
      "The Privacy screen: what's kept on this phone with current totals, " +
      "where requests go (directly to your provider), and how long things " +
      "stay.",
  },
  "09-backup": {
    id: "09-backup",
    alt:
      "The Backup screen: checkboxes for what goes into an encrypted file, " +
      "a note that backups never include passwords, sign-ins, provider " +
      "keys, browsing history, downloads or private tabs, and a Create " +
      "backup button.",
  },
  "10-tabs": {
    id: "10-tabs",
    alt:
      "The tab switcher: four of your tabs, then Taffy's tabs collapsed " +
      "into their own group, with a note that an amber edge marks a tab " +
      "Taffy opened for a task.",
  },
  "11-start-page-dark": {
    id: "11-start-page-dark",
    alt:
      "The start page in the dark theme, with a request typed into the box " +
      "and the Downloads, Workspaces, Tabs and Settings buttons along the " +
      "bottom.",
  },
  "12-taffy-dark": {
    id: "12-taffy-dark",
    alt:
      "Taffy answering a question about a recipe page in the dark theme, " +
      "naming its source and pointing out where the page's reviews " +
      "disagree.",
  },
  "x-ask-about-this-page": {
    id: "x-ask-about-this-page",
    alt:
      "The Ask Taffy panel with one recipe page attached and a question " +
      "typed. A note says the page's title and text go directly to your AI " +
      "provider and no other pages are opened.",
  },
};

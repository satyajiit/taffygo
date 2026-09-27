// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { links } from "../site";
import type { ScreenId } from "./screens";

/**
 * Every word on the home route ("/") lives here so the copy can be reviewed
 * and tested in one place. Content authority: docs/website/landing-page.md;
 * vocabulary authority: docs/voice-and-naming.md. Every claim is something
 * the captured screens show or docs/security/data-and-privacy.md states.
 */

/** A numbered note pinned to a point on a screen, in percent of its size. */
export type Callout = {
  readonly text: string;
  readonly x: number;
  readonly y: number;
};

export type TourShot = {
  readonly screen: ScreenId;
  /** Shown under the screen when a stop pairs two screens. */
  readonly caption?: string;
  /** Pins on the screen, explained in order beside it. */
  readonly callouts?: ReadonlyArray<Callout>;
};

export type TourStop = {
  readonly id: string;
  readonly title: string;
  readonly body: string;
  readonly shots: ReadonlyArray<TourShot>;
  readonly link?: { readonly label: string; readonly href: string };
};

export const hero = {
  title: "A browser that does the busywork with you.",
  lede:
    "TaffyGo is a Chromium browser for Android. It blocks ads and trackers, " +
    "and its built-in assistant, Taffy, can answer from the page you're on " +
    "or run an errand across a few sites while you watch. Taffy uses the AI " +
    "provider you connect.",
  facts: ["Free", "No account", "No TaffyGo server", "Open source"],
  factsLabel: "In short",
  stripLabel: "Three TaffyGo screens",
  strip: ["01-start-page", "04-errand-running", "12-taffy-dark"],
} as const satisfies {
  strip: ReadonlyArray<ScreenId>;
  [key: string]: unknown;
};

export const tour = {
  title: "What it does",
  lede:
    "Every screen on this site is a capture from the app on an Android " +
    "phone, taken in September 2026.",
  stops: [
    {
      id: "blocking",
      title: "Ads and trackers are blocked from the start",
      body:
        "Blocking is on for every site until you turn it off for one. The " +
        "page sheet counts what was stopped on the page you're reading.",
      shots: [
        {
          screen: "03-ad-blocking",
          callouts: [
            { text: "How many ads and trackers this page tried to load.", x: 20, y: 15 },
            {
              text: "The switch for this site alone. Turn it off if a site breaks.",
              x: 74,
              y: 26.5,
            },
          ],
        },
      ],
    },
    {
      id: "ask",
      title: "Ask about the page you're on",
      body:
        "Taffy answers from the page in front of you and says where each " +
        "part of the answer came from. When the page leaves something out, " +
        "Taffy says that too.",
      shots: [
        {
          screen: "02-taffy-on-a-page",
          callouts: [
            { text: "Your question.", x: 27, y: 15.6 },
            { text: "The source behind each part of the answer.", x: 90, y: 19 },
            { text: "What the page didn't say.", x: 94, y: 71 },
            { text: "Partly done, because something was missing.", x: 42, y: 81.5 },
          ],
        },
      ],
    },
    {
      id: "errands",
      title: "Hand over an errand",
      body:
        "Tell Taffy what you want done across a few sites. Before it starts, " +
        "Taffy says how many sites it may open and where the page text will " +
        "go. The result puts the answer first, then what each page said.",
      shots: [
        {
          screen: "05-asks-before-acting",
          caption: "Before it starts: what Taffy will do, and where your text goes.",
        },
        {
          screen: "06-results",
          caption: "After: the lower price first, then each page's details and source.",
        },
      ],
      link: { label: "Follow one errand", href: "/product/" },
    },
    {
      id: "providers",
      title: "Your provider, your key",
      body:
        "TaffyGo has no AI service of its own. You connect one: paste an API " +
        "key, sign in with a plan you already pay for, or add a provider by " +
        "hand. Requests go straight from your phone to that provider.",
      shots: [
        {
          screen: "07-your-provider-your-key",
          callouts: [
            {
              text: "A plan you pay for, an API key, or a provider you add yourself.",
              x: 50,
              y: 16.2,
            },
            { text: "Each provider says what it needs from you.", x: 72, y: 29.5 },
            { text: "Anything not listed can be added by hand.", x: 88, y: 96 },
          ],
        },
      ],
    },
    {
      id: "on-your-phone",
      title: "It stays on your phone",
      body:
        "History, workspaces, saved sign-ins and your keys are kept on the " +
        "phone. A backup is an encrypted file that you make and store " +
        "yourself, and it never holds passwords, sign-ins, provider keys or " +
        "browsing history.",
      shots: [
        {
          screen: "08-privacy",
          caption: "Privacy: what's kept, the current totals, and where requests go.",
        },
        {
          screen: "09-backup",
          caption: "Backup: you pick what goes into the encrypted file.",
        },
      ],
      link: { label: "Read the privacy policy", href: "/privacy/" },
    },
  ],
} as const satisfies { stops: ReadonlyArray<TourStop>; [key: string]: unknown };

/** The short bar that stays in view while the tour scrolls past. */
export const stickyGet = {
  label: "TaffyGo 1.0 is free.",
  play: "Google Play",
  apk: "Get the APK",
} as const;

export const meetTaffy = {
  title: "Taffy works when you ask",
  body:
    "Taffy is the only assistant in TaffyGo. It reads the pages you point " +
    "it at and tells you where their text will go before an errand starts. " +
    "You can take over, pause or stop it at any step.",
  imageAlt:
    "Painting: a person at a desk reads their phone while Taffy, a small " +
    "winged character, stands beside their arm.",
} as const;

export type Fact = {
  readonly label: string;
  readonly value: string;
  /** Shown on its own line under the value. */
  readonly link?: { readonly label: string; readonly href: string };
};

export const facts = {
  title: "What you're installing",
  rows: [
    { label: "Price", value: "Free. There is nothing to buy." },
    { label: "Account", value: "None." },
    { label: "Our servers", value: "None. Matterward Labs runs no server for TaffyGo." },
    { label: "AI", value: "Optional, through the provider you connect, with your key or your plan." },
    { label: "Blocking", value: "Ads and trackers, on by default, with a switch for each site." },
    { label: "Engine", value: "Chromium" },
    { label: "Phones", value: "Android 10 or later, on a 64-bit ARM processor" },
    {
      label: "Source",
      value: "Mozilla Public License 2.0",
      link: { label: "github.com/satyajiit/taffygo", href: links.repository },
    },
  ],
} as const satisfies { rows: ReadonlyArray<Fact>; [key: string]: unknown };

export const faqTitle = "Questions";

export const faq: ReadonlyArray<{ question: string; answer: string }> = [
  {
    question: "Is it free?",
    answer:
      "Yes. There is nothing to buy. Taffy runs on an " +
      "AI provider you connect, so if that provider charges, you pay them " +
      "directly.",
  },
  {
    question: "Do I need an account?",
    answer:
      "No. TaffyGo has no accounts, and Matterward Labs, which makes it, runs " +
      "no server for the app to sign in to.",
  },
  {
    question: "Can I use it without AI?",
    answer:
      "Yes. It's a complete browser with ad and tracker blocking. Until you " +
      "connect a provider, nothing is sent to one.",
  },
  {
    question: "Which AI providers work with Taffy?",
    answer:
      "Paste an API key for one of the providers the app lists, sign in with " +
      "a plan you already pay for, or add another provider yourself. " +
      "Requests go directly from your phone to the provider you chose.",
  },
  {
    question: "What leaves my phone?",
    answer:
      "Pages load from their own sites, as in any browser. When you ask " +
      "Taffy something, the text it needs and your request go to your AI " +
      "provider. Nothing goes to us.",
  },
  {
    question: "Should I install from Google Play or GitHub?",
    answer:
      "Either works, but pick one and keep to it: a copy from Google Play " +
      "won't update from a GitHub APK, or the other way round. Google Play " +
      "updates the app for you. With the APK, you download each new release " +
      "from GitHub yourself.",
  },
  {
    question: "Which phones does it run on?",
    answer:
      "Android 10 or later, on a 64-bit ARM processor. The screens and " +
      "controls are laid out for phones.",
  },
  {
    question: "Where is the source code?",
    answer:
      "On GitHub, under the Mozilla Public License 2.0. Bug reports, " +
      "questions and pull requests go there. The TaffyGo name, logo and the " +
      "Taffy character are not covered by that licence.",
  },
];

export const download = {
  title: "Get TaffyGo",
  body:
    "TaffyGo 1.0 is on Google Play, and each release on GitHub carries the " +
    "APK.",
  note:
    "A copy from Google Play won't update from a GitHub APK, or the other " +
    "way round, so pick one and keep to it.",
  bugLabel: "Report a bug on GitHub",
  bugHref: links.issues,
} as const;

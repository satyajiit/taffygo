// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { links } from "../site";

/**
 * Copy for the four standing documents: /privacy/, /delete-my-data/,
 * /terms/ and /licenses/. The privacy claims answer to
 * docs/security/data-and-privacy.md and to the app's own Privacy, Backup and
 * Help screens, and they were checked against the tree on 27 September 2026:
 *
 * - no operator origin is compiled in (the account plane's origin, key and
 *   client id, the managed route's origin and the asset origin are empty),
 *   so the app has no host of ours to reach (decisions 0200 and 0252);
 * - the Android analytics client records in memory and never leaves the
 *   device, and the app has no diagnostics switch, because it has nothing to
 *   send diagnostics to (decision 0253 removed the one that sent nothing);
 * - the Chromium build sets no Google API keys and `enable_reporting=false`;
 * - Android backup is off for the app (`allowBackup=false`, section 12.1).
 *
 * A change to any of those facts is a change to these pages.
 */

/** A run of text with optional links, so a paragraph can link inline. */
export type Inline = string | { readonly label: string; readonly href: string };

export type DocBlock =
  | { readonly kind: "p"; readonly text: ReadonlyArray<Inline> }
  | { readonly kind: "ul" | "ol"; readonly items: ReadonlyArray<ReadonlyArray<Inline>> };

export type DocSection = {
  readonly id: string;
  readonly heading: string;
  readonly blocks: ReadonlyArray<DocBlock>;
};

export type TrustDocument = {
  readonly title: string;
  readonly standfirst: string;
  readonly updated?: string;
  readonly sections: ReadonlyArray<DocSection>;
};

const UPDATED = "Last updated 27 September 2026";

/** Names the contents list beside a long document. */
export const contentsLabel = "On this page";

const p = (...text: Inline[]): DocBlock => ({ kind: "p", text });
const ul = (...items: Inline[][]): DocBlock => ({ kind: "ul", items });
const ol = (...items: Inline[][]): DocBlock => ({ kind: "ol", items });

export const privacyPolicy = {
  title: "Privacy policy",
  standfirst:
    "TaffyGo stores your browsing data on your phone. Matterward Labs Private Limited runs no server for the app and receives no app data. When you use AI, your request goes directly to your chosen provider.",
  updated: UPDATED,
  imageAlt:
    "Painting: a hand holds a phone whose screen is a small room, where " +
    "Taffy puts keepsakes on a shelf.",
  sections: [
    {
      id: "collect",
      heading: "What we collect",
      blocks: [
        p(
          "Nothing. TaffyGo has no accounts, no analytics and no crash " +
            "reporting that reaches us, and Matterward Labs runs no server " +
            "for any of it to reach.",
        ),
        p(
          "There is no diagnostics switch to turn on, because there is " +
            "nowhere for diagnostics to go. To tell us about a problem, " +
            "Help and feedback opens a draft in your own email app, or a " +
            "public issue on GitHub, and you decide whether to send it.",
        ),
      ],
    },
    {
      id: "on-the-phone",
      heading: "What stays on your phone",
      blocks: [
        p("TaffyGo keeps these in its own storage on the phone:"),
        ul(
          ["Browsing history, bookmarks, open tabs, cookies and downloads"],
          ["Saved sign-ins (passwords and passkeys) and saved details for forms"],
          ["Workspaces, the Library and Memory"],
          ["What Taffy did on each task"],
          ["Your AI provider keys, sealed by the phone's keystore"],
        ),
        p(
          "Ad and tracker blocking runs on the phone, from lists that ship " +
            "inside the app.",
        ),
      ],
    },
    {
      id: "leaves",
      heading: "What leaves your phone",
      blocks: [
        p(
          "Websites. Pages load from the sites you open, as in any browser, " +
            "and a search goes to the search engine set in the app.",
        ),
        p(
          "Your AI provider. When you ask Taffy something, the text Taffy " +
            "needs from the pages you chose, and your request, go directly " +
            "from your phone to the provider you connected, with your key or " +
            "your sign-in. Before an errand starts, Taffy tells you where the " +
            "page text will go. That provider's own terms and retention " +
            "apply, so read them.",
        ),
        p(
          "Taffy never sends passwords, card numbers or saved details to an " +
            "AI provider. With no provider connected, nothing is sent to one.",
        ),
      ],
    },
    {
      id: "backups",
      heading: "Backups",
      blocks: [
        p(
          "TaffyGo tells Android to leave its data out of the phone's own " +
            "backups and device transfers.",
        ),
        p(
          "You can make an encrypted backup file yourself from Backup in " +
            "Settings. You choose where the file is saved, and it can't be " +
            "opened without the recovery key you keep. It never includes " +
            "passwords, sign-ins, provider keys, browsing history, downloads " +
            "or private tabs.",
        ),
      ],
    },
    {
      id: "private-tabs",
      heading: "Private tabs",
      blocks: [p("Private tabs are forgotten when they close, by Taffy too.")],
    },
    {
      id: "deleting",
      heading: "Deleting your data",
      blocks: [
        p(
          "Everything TaffyGo keeps is on your phone, so that is where you " +
            "delete it. ",
          { label: "How to delete your data", href: "/delete-my-data/" },
          " covers each way.",
        ),
      ],
    },
    {
      id: "website",
      heading: "This website",
      blocks: [
        p(
          "taffygo.com sets no cookies and runs no analytics. Your browser " +
            "remembers your light or dark theme choice and caches public site " +
            "files for offline access and automatic updates. These stay on " +
            "your device. Clear this site’s data in your browser settings to remove them.",
        ),
        p(
          "The site is served by GitHub Pages, so GitHub receives the usual " +
            "request details, such as your IP address, when it serves a " +
            "page. ",
          { label: "GitHub's privacy statement", href: links.githubPrivacy },
          " covers that.",
        ),
      ],
    },
    {
      id: "changes",
      heading: "Changes and questions",
      blocks: [
        p(
          "When this policy changes, this page changes and the date at the " +
            "top moves. Ask about it in ",
          { label: "GitHub Discussions", href: links.discussions },
          ", and report a security problem ",
          { label: "privately on GitHub", href: links.securityReport },
          ".",
        ),
      ],
    },
  ],
} as const satisfies TrustDocument & { imageAlt: string };

export const deleteMyData = {
  title: "Delete your data",
  standfirst:
    "Clear your data in TaffyGo or in Android’s app settings. Matterward Labs Private Limited runs no server for TaffyGo and holds no copy of your app data.",
  sections: [
    {
      id: "in-the-app",
      heading: "Delete everything in TaffyGo",
      blocks: [
        ol(
          ["Open Settings, then Privacy."],
          ["Tap Delete everything."],
          ["Tap Delete and close TaffyGo to confirm."],
        ),
        p(
          "TaffyGo stops any open tasks and closes access to saved sign-ins " +
            "and your keys. Android then erases the app's data and closes it. " +
            "When you open TaffyGo again, the welcome screen shows that the " +
            "data is gone.",
        ),
      ],
    },
    {
      id: "browsing-only",
      heading: "Clear only your browsing data",
      blocks: [
        p(
          "Settings, Privacy, Clear browsing data removes history, cookies " +
            "and cached files. It leaves workspaces, the Library, Memory and " +
            "saved sign-ins alone.",
        ),
      ],
    },
    {
      id: "android",
      heading: "Or use Android",
      blocks: [
        p(
          "Clearing TaffyGo's storage in Android's settings for the app, or " +
            "uninstalling it, also removes everything it keeps. The menu " +
            "names differ from phone to phone.",
        ),
      ],
    },
    {
      id: "outside",
      heading: "What this doesn't reach",
      blocks: [
        ul(
          [
            "Files you downloaded or exported, and backup files you saved, " +
              "stay where you put them. Delete them there.",
          ],
          [
            "Your AI provider keeps what its own terms allow. Ask the " +
              "provider to delete it.",
          ],
          ["Accounts you made on websites are between you and those sites."],
        ),
        p({ label: "Read the privacy policy", href: "/privacy/" }),
      ],
    },
  ],
} as const satisfies TrustDocument;

export const terms = {
  title: "Terms of use",
  standfirst:
    "TaffyGo is free software published by Matterward Labs Private Limited. " +
    "These terms cover using the app and this website.",
  updated: UPDATED,
  sections: [
    {
      id: "licence",
      heading: "The licence",
      blocks: [
        p(
          "TaffyGo's source code is published under the Mozilla Public " +
            "License 2.0. You may use, study, change and share it under that " +
            "licence. The full text is in ",
          { label: "LICENSE", href: links.licence },
          " on GitHub.",
        ),
      ],
    },
    {
      id: "warranty",
      heading: "No warranty",
      blocks: [
        p(
          "TaffyGo is provided as is, without warranty, as sections 6 and 7 " +
            "of the licence set out. Taffy can misread a page or get a fact " +
            "wrong. Check anything that matters before you rely on it.",
        ),
      ],
    },
    {
      id: "services",
      heading: "Services you connect",
      blocks: [
        p(
          "Taffy uses the AI provider you connect, under that provider's " +
            "terms and prices. The websites you visit have their own terms. " +
            "Matterward Labs is not a party to either.",
        ),
      ],
    },
    {
      id: "marks",
      heading: "Names and marks",
      blocks: [
        p(
          "The licence does not cover the TaffyGo name, the logo or the Taffy " +
            "character. If you publish your own build, give it a different " +
            "name and artwork; ",
          { label: "TRADEMARKS.md", href: links.trademarks },
          " lists the files that means.",
        ),
        p(
          "Chromium is a project of The Chromium Authors. TaffyGo is built on " +
            "it and is not endorsed by them.",
        ),
      ],
    },
    {
      id: "changes",
      heading: "Changes",
      blocks: [
        p(
          "When these terms change, this page changes and the date at the " +
            "top moves. Questions go to ",
          { label: "GitHub Discussions", href: links.discussions },
          ".",
        ),
      ],
    },
  ],
} as const satisfies TrustDocument;

/** The notices for software this website itself serves. */
export const websiteNotices = [
  { name: "Instrument Serif", license: "SIL Open Font License 1.1", href: "/fonts/instrument-serif-OFL.txt" },
  { name: "SVGL brand icons", license: "MIT License; brand marks belong to their owners", href: "/licenses/svgl-mit.txt" },
  { name: "Space Grotesk", license: "SIL Open Font License 1.1", href: "/fonts/OFL.txt" },
  { name: "Lucide icons", license: "ISC License and notices", href: "/licenses/lucide-isc.txt" },
  { name: "Next.js", license: "MIT License", href: "/licenses/nextjs-mit.txt" },
  { name: "React and React DOM", license: "MIT License", href: "/licenses/react-mit.txt" },
  { name: "Tailwind CSS", license: "MIT License", href: "/licenses/tailwind-mit.txt" },
] as const;

export const licences = {
  title: "Open-source licences",
  standfirst:
    "TaffyGo's own source is published under the Mozilla Public License 2.0. " +
    "It is built on Chromium and other open-source software, each under its " +
    "own licence.",
  sections: [
    {
      id: "taffygo",
      heading: "TaffyGo",
      blocks: [
        p(
          "The source is at ",
          { label: "github.com/satyajiit/taffygo", href: links.repository },
          ". ",
          { label: "LICENSE", href: links.licence },
          " holds the licence text and ",
          { label: "NOTICE", href: links.notice },
          " says what it covers. The name, the logo and the Taffy character " +
            "are reserved, and ",
          { label: "TRADEMARKS.md", href: links.trademarks },
          " lists those files.",
        ),
      ],
    },
    {
      id: "browser",
      heading: "The browser's notices",
      blocks: [
        p(
          "The notices for Chromium and everything else packaged in the app " +
            "travel inside the app. In TaffyGo, open Settings, then About and " +
            "help, then Licences. The list is generated from the build " +
            "itself, so it names exactly what that copy of the app contains.",
        ),
      ],
    },
    {
      id: "website",
      heading: "This website",
      blocks: [
        p("The pages you are reading use:"),
        ul(
          ...websiteNotices.map((notice): Inline[] => [
            { label: notice.name, href: notice.href },
            `, ${notice.license}`,
          ]),
        ),
        p(
          "The typeface is served from this site rather than a font host, so " +
            "its licence travels with it.",
        ),
      ],
    },
  ],
} as const satisfies TrustDocument;

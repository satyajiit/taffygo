// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { links } from "../site";

/**
 * Copy for /contact/. TaffyGo is developed in the open, so every route
 * leads to the public repository. There is no mailbox and no form, and this
 * page collects nothing.
 */

export type ContactRoute = {
  readonly topic: string;
  readonly body: string;
  readonly action: string;
  readonly href: string;
};

export const contact = {
  title: "Contact",
  standfirst:
    "Reach the maintainers on GitHub. Use an issue for a bug, a discussion for a question, or a private report for a security problem.",
  routes: [
    {
      topic: "Bugs and feature requests",
      body: "Say what you did, what you expected and what happened instead.",
      action: "Open an issue",
      href: links.issues,
    },
    {
      topic: "Questions and ideas",
      body: "Discussions are public, so the answer helps the next person too.",
      action: "Start a discussion",
      href: links.discussions,
    },
    {
      topic: "A security problem",
      body:
        "Report it privately. Only the maintainers see the report until a " +
        "fix is out. Please don't open a public issue for it.",
      action: "Report a vulnerability",
      href: links.securityReport,
    },
    {
      topic: "Your data",
      body:
        "Clear browsing data, saved details, and files from TaffyGo on your phone.",
      action: "Delete your data",
      href: "/delete-my-data/",
    },
  ],
} as const satisfies { routes: ReadonlyArray<ContactRoute>; [key: string]: unknown };

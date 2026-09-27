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
    "TaffyGo is developed in the open on GitHub, and that is where to reach " +
    "the people who make it. There is no support inbox and no contact form.",
  routes: [
    {
      topic: "A bug, or something you want TaffyGo to do",
      body: "Say what you did, what you expected and what happened instead.",
      action: "Open an issue",
      href: links.issues,
    },
    {
      topic: "A question, or an idea to talk through",
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
        "Matterward Labs holds none. What TaffyGo keeps is on your phone, " +
        "and you can clear it there.",
      action: "Delete your data",
      href: "/delete-my-data/",
    },
  ],
} as const satisfies { routes: ReadonlyArray<ContactRoute>; [key: string]: unknown };

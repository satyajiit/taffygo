// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { ScreenId } from "./screens";

/**
 * Copy for /product/: one errand from start to finish, in real screens. The
 * app's own words (Ask Taffy, Take over, Partly done, Save workspace, Put
 * this away) are quoted as the app shows them.
 */

export type ErrandStep = {
  readonly id: string;
  readonly title: string;
  readonly body: string;
  readonly screen: ScreenId;
};

export const product = {
  title: "Hand Taffy an errand",
  lede:
    "An errand is a job that spans a few web pages: compare two listings, " +
    "pull the times out of a recipe, find a price. This is one errand, " +
    "captured on a phone from the request to the result.",
  imageAlt:
    "Painting: Taffy, a small winged character, holds up a card to a person " +
    "at a desk and waits for their answer.",
  stepsTitle: "One errand, three screens",
  steps: [
    {
      id: "ask",
      title: "Say what you want",
      body:
        "Type the request in Ask Taffy and attach the pages Taffy may read. " +
        "The panel says that those pages' titles and text go directly to " +
        "your AI provider, and that no other pages are opened.",
      screen: "x-ask-about-this-page",
    },
    {
      id: "watch",
      title: "Watch it work",
      body:
        "Taffy opens the pages in its own tabs and writes down what it is " +
        "doing in plain words. Take over, Pause and Stop stay at the bottom " +
        "the whole time.",
      screen: "04-errand-running",
    },
    {
      id: "result",
      title: "Read the result",
      body:
        "The answer comes first, then what each page said, with its source. " +
        "When a page left something out, the result is marked Partly done. " +
        "Save it as a workspace, or put it away.",
      screen: "06-results",
    },
  ],
  providersLink: {
    label: "Connecting your AI provider",
    href: "/#providers",
  },
  closing: {
    title: "Try it on your phone",
    body: "TaffyGo is free, with no account to make.",
  },
} as const satisfies { steps: ReadonlyArray<ErrandStep>; [key: string]: unknown };

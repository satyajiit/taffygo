// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { render, screen, within } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import BuiltForPhonesPage from "@/app/built-for-phones/page";
import { getTaffy } from "@/lib/content";
import { phoneSections, phonesHero, phonesRequirements } from "@/lib/content/phones";
import { screens } from "@/lib/content/screens";

describe("/built-for-phones/", () => {
  it("has one h1 and a section per claim, each on the screen that shows it", () => {
    const { container } = render(<BuiltForPhonesPage />);
    const h1 = screen.getAllByRole("heading", { level: 1 });
    expect(h1).toHaveLength(1);
    expect(h1[0]).toHaveTextContent(phonesHero.title);
    for (const section of phoneSections) {
      const article = container.querySelector(`article#${section.id}`) as HTMLElement;
      expect(article, section.id).not.toBeNull();
      expect(within(article).getByRole("heading", { level: 2 })).toHaveTextContent(section.title);
      for (const shot of section.shots) {
        expect(within(article).getByAltText(screens[shot.screen].alt)).toBeInTheDocument();
        if (shot.callouts) {
          expect(article.querySelectorAll(".pin")).toHaveLength(shot.callouts.length);
        }
      }
    }
  });

  it("does not claim the address bar is at the bottom", () => {
    const text = JSON.stringify({ phonesHero, phoneSections });
    expect(text).not.toMatch(/address bar (is|sits|lives) at the bottom/i);
    expect(text).not.toMatch(/one-handed mode/i);
  });

  it("states what it runs on and how to install it", () => {
    render(<BuiltForPhonesPage />);
    expect(screen.getByRole("heading", { name: phonesRequirements.title })).toBeInTheDocument();
    expect(screen.getByText(phonesRequirements.body)).toBeInTheDocument();
    expect(screen.getByRole("link", { name: getTaffy.playBadgeAlt })).toHaveAttribute(
      "href",
      getTaffy.playHref,
    );
  });
});

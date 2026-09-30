// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { fireEvent, render, screen, within } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import HomePage from "@/app/page";
import { Footer } from "@/components/Footer";
import { Header } from "@/components/Header";
import { SkipLink } from "@/components/SkipLink";
import { getTaffy } from "@/lib/content";
import { faq } from "@/lib/content/home";
import { links } from "@/lib/site";

function renderPage() {
  return render(<><SkipLink /><Header /><main id="main"><HomePage /></main><Footer /></>);
}

describe("landing page", () => {
  it("puts the skip link first and gives the page one descriptive heading", () => {
    const { container } = renderPage();
    expect(container.querySelector("a[href], button, summary")).toHaveAttribute("href", "#main");
    expect(screen.getAllByRole("heading", { level: 1 })).toHaveLength(1);
    expect(screen.getByRole("heading", { level: 1 })).toHaveTextContent("The AI-nativebrowser for Android.");
  });

  it("offers real install destinations in the hero and download section", () => {
    const { container } = renderPage();
    for (const selector of ['section[aria-labelledby="hero-title"]', "#get"]) {
      const section = within(container.querySelector(selector) as HTMLElement);
      expect(section.getByRole("link", { name: getTaffy.playBadgeAlt })).toHaveAttribute("href", links.googlePlay);
      expect(section.getByRole("link", { name: getTaffy.apkLabel })).toHaveAttribute("href", links.releases);
    }
    expect(screen.getAllByRole("link", { name: /Star on GitHub/ })).toHaveLength(2);
  });

  it("shows the idle new-tab UI with the app’s scene and can pause motion", () => {
    renderPage();
    expect(screen.getByText("Where to today?")).toBeInTheDocument();
    expect(screen.getByAltText(/app’s morning scene/)).toHaveAttribute("src", "/start-scenes/morning-320.webp");
    expect(screen.getByRole("textbox", { name: "Try the address box" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Preview downloads" })).toBeInTheDocument();
    expect(screen.getByAltText(/winged assistant/)).toHaveAttribute("src", "/cutouts/taffy-240.webp");
    fireEvent.click(screen.getByRole("button", { name: "Pause motion" }));
    expect(document.documentElement.dataset.motionPaused).toBe("true");
    fireEvent.click(screen.getByRole("button", { name: "Play motion" }));
    expect(document.documentElement.dataset.motionPaused).toBe("false");
  });

  it("opens navigation for touch and closes it with Escape", () => {
    const { container } = renderPage();
    const menu = container.querySelector(".explore-menu") as HTMLDetailsElement;
    menu.open = true;
    fireEvent.keyDown(menu.querySelector("summary")!, { key: "Escape" });
    expect(menu.open).toBe(false);
    expect(document.activeElement).toBe(menu.querySelector("summary"));
    expect(within(menu).getByRole("link", { name: /AI models/, hidden: true })).toHaveAttribute("href", "/providers/");
  });

  it("gives the visible FAQ matching machine-readable answers", () => {
    const { container } = renderPage();
    const structured = [...container.querySelectorAll('script[type="application/ld+json"]')].map(node => JSON.parse(node.textContent!));
    const data = structured.find(item => item["@type"] === "FAQPage");
    expect(data.mainEntity).toHaveLength(faq.length);
    for (const item of faq) {
      const question = screen.getByText(item.question, { selector: "summary" });
      expect(question.closest("details")).toHaveTextContent(item.answer);
      expect(data.mainEntity).toContainEqual({ "@type": "Question", name: item.question, acceptedAnswer: { "@type": "Answer", text: item.answer } });
    }
  });

  it("credits Matterward with its real brand asset and links privacy controls", () => {
    renderPage();
    expect(screen.getByAltText("Matterward Labs")).toHaveAttribute("src", "/brand/matterward-labs.svg");
    expect(screen.getByRole("link", { name: "Delete your data" })).toHaveAttribute("href", "/delete-my-data/");
    expect(screen.getByText(/This site sets no cookies/)).toBeInTheDocument();
    for (const link of screen.getAllByRole("link", { name: /Request a feature/ })) {
      expect(link).toHaveAttribute("href", "https://github.com/satyajiit/taffygo/issues/new?template=feature_request.yml");
    }
    for (const link of screen.getAllByRole("link", { name: /Report a bug/ })) {
      expect(link).toHaveAttribute("href", "https://github.com/satyajiit/taffygo/issues/new?template=bug_report.yml");
    }
  });
});

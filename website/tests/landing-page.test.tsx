// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { render, screen, within } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import HomePage from "@/app/page";
import { Footer } from "@/components/Footer";
import { Header } from "@/components/Header";
import { SkipLink } from "@/components/SkipLink";
import { footer, getTaffy, header } from "@/lib/content";
import {
  download,
  facts,
  faq,
  hero,
  meetTaffy,
  stickyGet,
  tour,
  type TourShot,
} from "@/lib/content/home";
import { screens } from "@/lib/content/screens";

function renderPage() {
  return render(
    <>
      <SkipLink />
      <Header />
      <main id="main">
        <HomePage />
      </main>
      <Footer />
    </>,
  );
}

describe("landing page", () => {
  it("puts a skip link first in the focus order", () => {
    const { container } = renderPage();
    const focusable = container.querySelectorAll("a[href], button, summary");
    expect(focusable[0]).toHaveAttribute("href", "#main");
    expect(focusable[0]).toHaveTextContent("Skip to content");
  });

  it("has exactly one first-level heading, and it is the promise", () => {
    renderPage();
    const headings = screen.getAllByRole("heading", { level: 1 });
    expect(headings).toHaveLength(1);
    expect(headings[0]).toHaveTextContent(hero.title);
  });

  it("offers Google Play and the APK in the hero and in the download section", () => {
    const { container } = renderPage();
    for (const selector of ['section[aria-labelledby="hero-title"]', "#get"]) {
      const region = container.querySelector(selector) as HTMLElement;
      expect(region, selector).not.toBeNull();
      const play = within(region).getByRole("link", { name: getTaffy.playBadgeAlt });
      expect(play).toHaveAttribute("href", getTaffy.playHref);
      expect(within(region).getByRole("link", { name: getTaffy.apkLabel })).toHaveAttribute(
        "href",
        getTaffy.apkHref,
      );
      expect(within(region).getByRole("link", { name: getTaffy.sourceLabel })).toHaveAttribute(
        "href",
        getTaffy.sourceHref,
      );
    }
  });

  it("shows the hero strip as real screens, the first one fetched first", () => {
    const { container } = renderPage();
    const strip = screen.getByRole("group", { name: hero.stripLabel });
    const images = within(strip).getAllByRole("img");
    expect(images.map((image) => image.getAttribute("alt"))).toEqual(
      hero.strip.map((id) => screens[id].alt),
    );
    expect(images[0]).toHaveAttribute("fetchpriority", "high");
    expect(images[0]).toHaveAttribute("loading", "eager");
    for (const image of images.slice(1)) {
      expect(image).toHaveAttribute("loading", "lazy");
    }
    expect(container.querySelector(".hero-strip")).not.toBeNull();
  });

  it("gives every tour stop its screens, and one pin per numbered note", () => {
    const { container } = renderPage();
    for (const stop of tour.stops) {
      const article = container.querySelector(`article#${stop.id}`) as HTMLElement;
      expect(article, stop.id).not.toBeNull();
      expect(within(article).getByRole("heading", { level: 3 })).toHaveTextContent(stop.title);
      for (const shot of stop.shots as ReadonlyArray<TourShot>) {
        expect(within(article).getByAltText(screens[shot.screen].alt)).toBeInTheDocument();
        if (shot.caption) expect(article).toHaveTextContent(shot.caption);
        if (shot.callouts) {
          const pins = article.querySelectorAll(".pin");
          const notes = article.querySelectorAll(".callouts li");
          expect(pins).toHaveLength(shot.callouts.length);
          expect(notes).toHaveLength(shot.callouts.length);
          expect(pins[0]!.closest('[aria-hidden="true"]')).not.toBeNull();
        }
      }
    }
    expect(container.querySelector("article#providers")).not.toBeNull();
  });

  it("keeps a get bar in the tour that stays in view while it scrolls", () => {
    const { container } = renderPage();
    const bar = container.querySelector("#tour .sticky-get") as HTMLElement;
    expect(bar).not.toBeNull();
    expect(within(bar).getByRole("link", { name: stickyGet.play })).toHaveAttribute(
      "href",
      getTaffy.playHref,
    );
    expect(within(bar).getByRole("link", { name: stickyGet.apk })).toHaveAttribute(
      "href",
      getTaffy.apkHref,
    );
  });

  it("introduces Taffy with a described painting", () => {
    renderPage();
    expect(screen.getByRole("heading", { name: meetTaffy.title })).toBeInTheDocument();
    expect(screen.getByAltText(meetTaffy.imageAlt)).toHaveAttribute(
      "srcset",
      expect.stringContaining("/art/website-hero-1600.webp 1600w"),
    );
  });

  it("sets the facts out as a list of terms, each with its value", () => {
    const { container } = renderPage();
    const list = container.querySelector("#facts dl.spec") as HTMLElement;
    expect(list).not.toBeNull();
    const terms = [...list.querySelectorAll("dt")].map((term) => term.textContent);
    expect(terms).toEqual(facts.rows.map((row) => row.label));
    const values = [...list.querySelectorAll("dd")];
    for (const [index, row] of facts.rows.entries()) {
      expect(values[index]).toHaveTextContent(row.value);
    }
    expect(within(list).getByRole("link", { name: "github.com/satyajiit/taffygo" })).toHaveAttribute(
      "href",
      "https://github.com/satyajiit/taffygo",
    );
  });

  it("answers each question in a native disclosure", () => {
    const { container } = renderPage();
    const details = container.querySelectorAll("#questions details");
    expect(details).toHaveLength(faq.length);
    for (const [index, item] of faq.entries()) {
      expect(details[index]!.querySelector("summary")).toHaveTextContent(item.question);
    }
  });

  it("ends with the download section the header points at", () => {
    const { container } = renderPage();
    expect(header.getHref).toBe("/#get");
    const get = container.querySelector("section#get") as HTMLElement;
    expect(within(get).getByRole("heading", { level: 2 })).toHaveTextContent(download.title);
    expect(get).toHaveTextContent(download.note);
    expect(within(get).getByRole("link", { name: download.bugLabel })).toHaveAttribute(
      "href",
      download.bugHref,
    );
  });

  it("links every page from the footer on the dark surface", () => {
    renderPage();
    const nav = screen.getByRole("navigation", { name: footer.navLabel });
    for (const link of footer.links) {
      expect(within(nav).getByRole("link", { name: link.label })).toHaveAttribute(
        "href",
        link.href,
      );
    }
    expect(nav.closest("footer")).toHaveAttribute("data-taffy-theme", "dark");
  });

  it("draws no device chrome around a screen", () => {
    const { container } = renderPage();
    for (const frame of container.querySelectorAll(".phone-frame")) {
      expect(frame.querySelectorAll("svg")).toHaveLength(0);
      expect(frame.children[0]!.tagName).toBe("IMG");
    }
  });
});

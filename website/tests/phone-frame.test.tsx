// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import { CalloutList, PhoneFrame, screenSources } from "@/components/PhoneFrame";
import { SCREEN_SIZES, screens, type ScreenId } from "@/lib/content/screens";

const callouts = [
  { text: "First note.", x: 20, y: 15 },
  { text: "Second note.", x: 74, y: 26.5 },
] as const;

describe("PhoneFrame", () => {
  it("shows the captured screen at two widths, with its own alt text", () => {
    render(<PhoneFrame screen="03-ad-blocking" />);
    const image = screen.getByRole("img");
    expect(image).toHaveAttribute("alt", screens["03-ad-blocking"].alt);
    expect(image).toHaveAttribute("src", "/screens/03-ad-blocking-360.webp");
    expect(image).toHaveAttribute(
      "srcset",
      "/screens/03-ad-blocking-360.webp 360w, /screens/03-ad-blocking-720.webp 720w",
    );
    expect(image).toHaveAttribute("width", String(SCREEN_SIZES[0].width));
    expect(image).toHaveAttribute("height", String(SCREEN_SIZES[0].height));
    expect(image).toHaveAttribute("loading", "lazy");
    expect(image).toHaveAttribute("decoding", "async");
    expect(image).not.toHaveAttribute("fetchpriority");
  });

  it("fetches a priority screen eagerly", () => {
    render(<PhoneFrame screen="01-start-page" priority />);
    const image = screen.getByRole("img");
    expect(image).toHaveAttribute("loading", "eager");
    expect(image).toHaveAttribute("fetchpriority", "high");
  });

  it("numbers its pins in order and hides them from assistive technology", () => {
    const { container } = render(<PhoneFrame screen="03-ad-blocking" callouts={callouts} />);
    const pins = [...container.querySelectorAll(".pin")] as HTMLElement[];
    expect(pins.map((pin) => pin.textContent)).toEqual(["1", "2"]);
    expect(pins[1]!.style.getPropertyValue("--x")).toBe("74");
    expect(pins[1]!.style.getPropertyValue("--y")).toBe("26.5");
    const layer = pins[0]!.parentElement!;
    expect(layer).toHaveAttribute("aria-hidden", "true");
    // Every annotated capture is a light screen, so the pins keep the light
    // projection in either site theme.
    expect(layer).toHaveAttribute("data-taffy-theme", "light");
  });

  it("puts the words for each pin in an ordered list", () => {
    render(<CalloutList callouts={callouts} />);
    const items = screen.getAllByRole("listitem");
    expect(items.map((item) => item.textContent)).toEqual(["First note.", "Second note."]);
    expect(items[0]!.closest("ol")).not.toBeNull();
  });

  it("captions a screen below the frame, not over the capture", () => {
    const { container } = render(<PhoneFrame screen="08-privacy" caption="A caption." />);
    const caption = container.querySelector("figcaption")!;
    expect(caption).toHaveTextContent("A caption.");
    expect(caption.closest(".phone-frame")).toBeNull();
  });

  it("names a screen for every capture the site shows", () => {
    for (const id of Object.keys(screens) as ScreenId[]) {
      expect(screens[id].id).toBe(id);
      expect(screens[id].alt.length, id).toBeGreaterThan(40);
      expect(screenSources(id).src).toBe(`/screens/${id}-360.webp`);
    }
  });
});

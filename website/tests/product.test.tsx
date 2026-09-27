// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { render, screen, within } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import ProductPage from "@/app/product/page";
import { getTaffy } from "@/lib/content";
import { product } from "@/lib/content/product";
import { screens } from "@/lib/content/screens";

describe("/product/", () => {
  it("has one h1 and the transparent Taffy asset beside it", () => {
    render(<ProductPage />);
    const h1 = screen.getAllByRole("heading", { level: 1 });
    expect(h1).toHaveLength(1);
    expect(h1[0]).toHaveTextContent(product.title);
    expect(screen.getByAltText("Taffy, the browser’s assistant")).toHaveAttribute(
      "src",
      "/cutouts/taffy.webp",
    );
  });

  it("walks one errand in three numbered steps, each on a real screen", () => {
    render(<ProductPage />);
    const steps = screen.getByRole("heading", { name: product.stepsTitle });
    const list = steps.parentElement!.querySelector("ol.steps") as HTMLElement;
    const items = within(list).getAllByRole("listitem");
    expect(items).toHaveLength(product.steps.length);
    for (const [index, step] of product.steps.entries()) {
      const item = items[index]!;
      expect(within(item).getByRole("heading", { level: 3 })).toHaveTextContent(step.title);
      expect(within(item).getByAltText(screens[step.screen].alt)).toBeInTheDocument();
    }
  });

  it("links to the provider directory and ends with a way to install", () => {
    render(<ProductPage />);
    expect(screen.getByRole("link", { name: product.providersLink.label })).toHaveAttribute(
      "href",
      "/providers/",
    );
    expect(screen.getByRole("link", { name: getTaffy.apkLabel })).toHaveAttribute(
      "href",
      getTaffy.apkHref,
    );
  });
});

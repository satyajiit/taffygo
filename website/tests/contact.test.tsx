// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import ContactPage from "@/app/contact/page";
import { contact } from "@/lib/content/contact";
import { links } from "@/lib/site";

describe("/contact/", () => {
  it("has one h1 and directs visitors to GitHub", () => {
    const { container } = render(<ContactPage />);
    const h1 = screen.getAllByRole("heading", { level: 1 });
    expect(h1).toHaveLength(1);
    expect(h1[0]).toHaveTextContent(contact.title);
    expect(contact.standfirst).toMatch(/Reach the maintainers on GitHub/);
    expect(container.querySelector("form, input, textarea")).toBeNull();
    expect(container.querySelector('a[href^="mailto:"]')).toBeNull();
  });

  it("routes bugs, questions and security reports to GitHub", () => {
    render(<ContactPage />);
    const expected = [links.issues, links.discussions, links.securityReport, "/delete-my-data/"];
    expect(contact.routes.map((route) => route.href)).toEqual(expected);
    for (const route of contact.routes) {
      expect(screen.getByRole("heading", { name: route.topic })).toBeInTheDocument();
      expect(screen.getByRole("link", { name: route.action })).toHaveAttribute(
        "href",
        route.href,
      );
    }
  });

  it("asks for security problems to be reported privately", () => {
    const security = contact.routes.find((route) => route.href === links.securityReport)!;
    expect(security.body).toMatch(/privately/);
    expect(security.body).toMatch(/don't open a public issue/);
  });
});

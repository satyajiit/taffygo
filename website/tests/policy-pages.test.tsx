// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { render, screen, within } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import DeleteMyDataPage from "@/app/delete-my-data/page";
import LicensesPage from "@/app/licenses/page";
import PrivacyPage from "@/app/privacy/page";
import TermsPage from "@/app/terms/page";
import {
  deleteMyData,
  licences,
  privacyPolicy,
  terms,
  websiteNotices,
  type TrustDocument,
} from "@/lib/content/trust";
import { links } from "@/lib/site";

const pages = [
  ["/privacy/", PrivacyPage, privacyPolicy],
  ["/delete-my-data/", DeleteMyDataPage, deleteMyData],
  ["/terms/", TermsPage, terms],
  ["/licenses/", LicensesPage, licences],
] as const;

function text(document: TrustDocument): string {
  return JSON.stringify(document);
}

describe("standing documents", () => {
  it.each(pages)("%s renders one h1, its standfirst and every section", (_route, Page, doc) => {
    const { container } = render(<Page />);
    const h1 = screen.getAllByRole("heading", { level: 1 });
    expect(h1).toHaveLength(1);
    expect(h1[0]).toHaveTextContent(doc.title);
    expect(screen.getByText(doc.standfirst)).toBeInTheDocument();
    const contents = container.querySelector('nav[aria-labelledby="doc-contents"]') as HTMLElement;
    for (const section of doc.sections) {
      const region = container.querySelector(`section#${section.id}`) as HTMLElement;
      expect(region, section.id).not.toBeNull();
      expect(within(region).getByRole("heading", { level: 2 })).toHaveTextContent(section.heading);
      expect(within(contents).getByRole("link", { name: section.heading })).toHaveAttribute(
        "href",
        `#${section.id}`,
      );
    }
  });

  it("keeps the retired pre-release and wishlist wording off every document", () => {
    for (const [, , doc] of pages) {
      expect(text(doc)).not.toMatch(
        /wishlist|not (yet )?released|before (TaffyGo )?1\.0 ships|publishes before|in development/i,
      );
    }
  });
});

describe("/privacy/", () => {
  it("says nothing is collected and no server exists to receive it", () => {
    expect(privacyPolicy.standfirst).toMatch(/runs no server/);
    const collect = privacyPolicy.sections.find((section) => section.id === "collect")!;
    expect(JSON.stringify(collect)).toMatch(/Nothing\./);
    expect(JSON.stringify(collect)).toMatch(/no diagnostics switch/);
    expect(JSON.stringify(collect)).not.toMatch(/Share diagnostics/);
  });

  it("names the two places data goes: websites and the provider you connect", () => {
    const leaves = JSON.stringify(privacyPolicy.sections.find((s) => s.id === "leaves"));
    expect(leaves).toMatch(/Websites\./);
    expect(leaves).toMatch(/directly from your phone to the provider you connected/);
    expect(leaves).toMatch(/never sends passwords, card numbers or saved details/);
  });

  it("describes the website's own hosting honestly", () => {
    const website = JSON.stringify(privacyPolicy.sections.find((s) => s.id === "website"));
    expect(website).toMatch(/GitHub Pages/);
    expect(website).toMatch(/no cookies/);
    expect(website).toContain(links.githubPrivacy);
  });

  it("shows the painting with its description", () => {
    render(<PrivacyPage />);
    expect(screen.getByAltText(privacyPolicy.imageAlt)).toHaveAttribute(
      "src",
      "/art/section-on-your-phone-720.webp",
    );
  });

  it("links to the deletion page on this site", () => {
    render(<PrivacyPage />);
    expect(screen.getByRole("link", { name: "How to delete your data" })).toHaveAttribute(
      "href",
      "/delete-my-data/",
    );
  });
});

describe("/delete-my-data/", () => {
  it("answers first that there is nothing on our side to delete", () => {
    expect(deleteMyData.standfirst).toMatch(/^There is nothing on our side to delete\./);
  });

  it("gives the in-app steps in order, then the Android route", () => {
    render(<DeleteMyDataPage />);
    const region = document.querySelector("section#in-the-app") as HTMLElement;
    const steps = within(region).getAllByRole("listitem");
    expect(steps.map((step) => step.textContent)).toEqual([
      "Open Settings, then Privacy.",
      "Tap Delete everything.",
      "Tap Delete and close TaffyGo to confirm.",
    ]);
    expect(steps[0]!.closest("ol")).not.toBeNull();
    expect(document.querySelector("section#android")).not.toBeNull();
    expect(document.querySelector("section#outside")).toHaveTextContent(/AI provider/);
  });
});

describe("/terms/", () => {
  it("rests on the MPL, disclaims warranty and keeps the name out of the grant", () => {
    render(<TermsPage />);
    expect(screen.getByRole("link", { name: "LICENSE" })).toHaveAttribute("href", links.licence);
    expect(text(terms)).toMatch(/Mozilla Public License 2\.0/);
    expect(text(terms)).toMatch(/without warranty/);
    expect(screen.getByRole("link", { name: "TRADEMARKS.md" })).toHaveAttribute(
      "href",
      links.trademarks,
    );
  });
});

describe("/licenses/", () => {
  it("links the project's licence files and every notice this website serves", () => {
    render(<LicensesPage />);
    expect(screen.getByRole("link", { name: "NOTICE" })).toHaveAttribute("href", links.notice);
    for (const notice of websiteNotices) {
      expect(screen.getByRole("link", { name: notice.name })).toHaveAttribute(
        "href",
        notice.href,
      );
    }
  });

  it("sends readers to the app for the browser's own notices", () => {
    expect(text(licences)).toMatch(/Settings, then About and help, then Licences/);
  });
});

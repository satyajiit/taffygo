// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// @vitest-environment node
import { existsSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import * as shared from "@/lib/content";
import * as contact from "@/lib/content/contact";
import * as home from "@/lib/content/home";
import * as phones from "@/lib/content/phones";
import * as product from "@/lib/content/product";
import * as screenModule from "@/lib/content/screens";
import * as trust from "@/lib/content/trust";
import { links, routeMeta, site } from "@/lib/site";

/**
 * The copy is the product here, so it is tested like code. Vocabulary rules
 * come from docs/voice-and-naming.md; content rules from
 * docs/website/landing-page.md.
 */

function collectStrings(value: unknown, out: string[] = []): string[] {
  if (typeof value === "string") {
    out.push(value);
  } else if (Array.isArray(value)) {
    for (const item of value) collectStrings(item, out);
  } else if (value && typeof value === "object") {
    for (const item of Object.values(value)) collectStrings(item, out);
  }
  return out;
}

const modules = { shared, contact, home, phones, product, screenModule, trust };
const copy = [
  ...collectStrings(modules),
  site.title,
  site.description,
  site.ogImageAlt,
  ...collectStrings(routeMeta),
];

// Mirrors tools/lib/docs_lint.py so the site cannot drift from the guide.
const BANNED: ReadonlyArray<readonly [string, RegExp]> = [
  ["release vocabulary", /\balpha\b|\bbeta\b|\bMVP\b|\brollout\b|\[Future\]/i],
  ["retired names", /AI Notch|Intent Box|\bSoul\b|\bEA\b|User Drives|AI Drives|Take Control/],
  ["retired structure", /\bG[0-8]\b|\bV[1-7]\b|\bR[1-4]\b|multi-agent|custom agent/i],
  ["internal vocabulary", /\bprovenance\b|\bartifact\b|\bmilestone\b|\bspike\b/i],
  ["overclaiming", /always listening|autonomous|permanent memory|understands the page|safe action/i],
  ["retired route", /wishlist/i],
  ["filler", /\bseamless|\bleverage|\brobust\b|\bempower|cutting-edge|game.?changer|supercharge/i],
];

// TaffyGo 1.0 is out. None of the pre-release wording may survive.
const PRE_RELEASE =
  /not (yet )?released|not published yet|in development|nothing to install|coming soon|before (TaffyGo )?1\.0 ships|join the waitlist/i;

// What the app is built from and signed with stays out of the site.
const PRIVATE_MATTER = /\.jks\b|signing (key|certificate)|upload key|keystore (file|password)/i;

/** Every href in the content modules, wherever it sits. */
function hrefs(value: unknown, out: string[] = []): string[] {
  if (Array.isArray(value)) {
    for (const item of value) hrefs(item, out);
  } else if (value && typeof value === "object") {
    for (const [key, item] of Object.entries(value)) {
      if (/href$/i.test(key) && typeof item === "string") out.push(item);
      else hrefs(item, out);
    }
  }
  return out;
}

const OFF_SITE = [
  /^https:\/\/github\.com\/satyajiit\/taffygo(\/|$)/,
  /^https:\/\/play\.google\.com\/store\/apps\/details\?id=com\.taffygo\.browser$/,
  /^https:\/\/docs\.github\.com\//,
];

describe("copy", () => {
  it("uses no banned vocabulary", () => {
    for (const line of copy) {
      for (const [rule, pattern] of BANNED) {
        expect(pattern.test(line), `${rule}: ${line}`).toBe(false);
      }
    }
  });

  it("describes the released app, with no pre-release wording left", () => {
    for (const line of copy) {
      expect(PRE_RELEASE.test(line), line).toBe(false);
    }
  });

  it("says nothing about how the app is signed", () => {
    for (const line of copy) {
      expect(PRIVATE_MATTER.test(line), line).toBe(false);
    }
  });

  it("uses no em dash in short copy", () => {
    for (const line of copy) {
      expect(line.includes("—"), line).toBe(false);
    }
  });

  it("states the four facts of the 1.0 app in the hero", () => {
    expect(home.hero.facts).toEqual(["Free", "No account", "No TaffyGo server", "Open source"]);
    expect(site.description).toMatch(/free, open-source/);
    expect(site.description).toMatch(/No account/);
  });

  it("points every link at a real page, file or the project's own places", () => {
    for (const href of hrefs(modules)) {
      if (href.startsWith("https://")) {
        expect(
          OFF_SITE.some((pattern) => pattern.test(href)),
          `unexpected destination ${href}`,
        ).toBe(true);
      } else {
        const [path, hash] = href.split("#");
        if (path === "" && hash) continue;
        const isRoute = path! in routeMeta;
        const isFile = existsSync(join(process.cwd(), "public", path!));
        expect(isRoute || isFile, `no page or file at ${href}`).toBe(true);
      }
    }
  });

  it("names the three ways to get or read TaffyGo exactly", () => {
    expect(shared.getTaffy.playHref).toBe(
      "https://play.google.com/store/apps/details?id=com.taffygo.browser",
    );
    expect(shared.getTaffy.apkHref).toBe("https://github.com/satyajiit/taffygo/releases/latest");
    expect(shared.getTaffy.sourceHref).toBe("https://github.com/satyajiit/taffygo");
    expect(links.securityReport).toBe(
      "https://github.com/satyajiit/taffygo/security/advisories/new",
    );
  });

  it("gives every question and every tour stop distinct wording", () => {
    const titles = [
      ...home.faq.map((item) => item.question),
      ...home.tour.stops.map((stop) => stop.title),
      ...phones.phoneSections.map((section) => section.title),
      ...product.product.steps.map((step) => step.title),
    ];
    expect(new Set(titles).size).toBe(titles.length);
  });

  it("pins the canonical domain and publisher without inventing an address", () => {
    expect(site.url).toBe("https://taffygo.com");
    expect(site.publisher).toBe("Matterward Labs Private Limited");
    expect("contactEmail" in site).toBe(false);
    for (const line of copy) {
      expect(/@taffygo\.com|mailto:/i.test(line), line).toBe(false);
    }
  });

  it("lists all exploration routes and every page in the footer", () => {
    expect(shared.header.nav.map((item) => item.href)).toEqual([
      "/product/",
      "/use-cases/",
      "/providers/",
      "/technology/",
      "/built-for-phones/",
      "/privacy/",
    ]);
    const footerHrefs = shared.footer.links.map((link) => link.href);
    for (const route of Object.keys(routeMeta).filter((route) => route !== "/")) {
      expect(footerHrefs, route).toContain(route);
    }
    expect(footerHrefs).toContain(links.repository);
  });

  it("keeps every link label short enough to sit on one line at 320px", () => {
    const labels = [
      ...shared.header.nav.map((item) => item.label),
      shared.header.getLabel,
      shared.header.getShortLabel,
      ...shared.footer.links.map((link) => link.label),
      shared.getTaffy.apkLabel,
      shared.getTaffy.sourceLabel,
      home.stickyGet.play,
      home.stickyGet.apk,
      home.download.bugLabel,
      ...home.tour.stops.flatMap((stop) => ("link" in stop && stop.link ? [stop.link.label] : [])),
      ...contact.contact.routes.map((route) => route.action),
      product.product.providersLink.label,
    ];
    for (const label of labels) {
      expect(label.length, label).toBeLessThanOrEqual(30);
    }
  });
});

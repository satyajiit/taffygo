// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// @vitest-environment node
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { metadata, viewport } from "@/app/layout";
import manifest from "@/app/manifest";
import { metadata as movedMetadata } from "@/app/privacy-policy/page";
import robots from "@/app/robots";
import sitemap from "@/app/sitemap";
import { appTheme } from "@/lib/app-theme";
import {
  absoluteUrl,
  pageMetadata,
  redirects,
  routeMeta,
  routes,
  site,
  type RoutePath,
} from "@/lib/site";

const root = fileURLToPath(new URL("..", import.meta.url));
const pageFile = (route: string) => join(root, "app", route, "page.tsx");

describe("metadata", () => {
  it("uses the page's own promise as the description", () => {
    expect(metadata.title).toEqual({
      default: site.title,
      template: `%s | ${site.name}`,
    });
    expect(metadata.description).toBe(site.description);
    expect(metadata.alternates?.canonical).toBe("/");
    expect(site.url).toBe("https://taffygo.com");
  });

  it("ships a 1200x630 social card with a described image", () => {
    const image = Array.isArray(metadata.openGraph?.images)
      ? metadata.openGraph.images[0]
      : undefined;
    expect(image).toMatchObject({ url: site.ogImage, width: 1200, height: 630 });
    expect(site.ogImageAlt.length).toBeGreaterThan(10);
    const twitterImage = Array.isArray(metadata.twitter?.images)
      ? metadata.twitter.images[0]
      : undefined;
    expect(twitterImage).toMatchObject({
      url: site.ogImage,
      width: 1200,
      height: 630,
      alt: site.ogImageAlt,
    });
    expect(existsSync(join(root, "public", site.ogImage))).toBe(true);
  });

  it("declares both app theme colours", () => {
    expect(viewport.colorScheme).toBe("light dark");
    expect(viewport.themeColor).toEqual([
      { media: "(prefers-color-scheme: light)", color: appTheme.lightSurface },
      { media: "(prefers-color-scheme: dark)", color: appTheme.darkSurface },
    ]);
  });

  it("ships a manifest whose icons exist, on the base path of this build", () => {
    expect(manifest()).toMatchObject({
      name: site.name,
      start_url: "/",
      background_color: appTheme.lightSurface,
      theme_color: appTheme.darkSurface,
      icons: [
        { src: "/icon.png", sizes: "192x192", type: "image/png" },
        { src: "/apple-icon.png", sizes: "180x180", type: "image/png" },
      ],
    });
    for (const icon of ["icon.png", "apple-icon.png", "favicon.ico"]) {
      expect(existsSync(join(root, "app", icon)), icon).toBe(true);
    }
  });

  it("describes a free Android app and its publisher in JSON-LD", () => {
    const layout = readFileSync(join(root, "app", "layout.tsx"), "utf8");
    expect(layout).toContain('"@type": "SoftwareApplication"');
    expect(layout).toContain('applicationCategory: "BrowserApplication"');
    expect(layout).toContain('operatingSystem: "Android 10 or later"');
    expect(layout).toContain('"@type": "Organization"');
    expect(layout).toContain("installUrl: links.googlePlay");
    expect(layout).toContain("downloadUrl: links.releases");
    expect(layout).toContain('price: "0"');
    expect(layout).not.toContain("softwareVersion");
    expect(layout).not.toContain("aggregateRating");
    expect(site.publisher).toBe("Matterward Labs Private Limited");
  });

  it("covers all eight pages in the route contract", () => {
    expect(routes).toEqual([
      "/",
      "/product/",
      "/built-for-phones/",
      "/contact/",
      "/privacy/",
      "/delete-my-data/",
      "/terms/",
      "/licenses/",
    ]);
    for (const route of routes) {
      expect(routeMeta[route].title.length).toBeGreaterThan(5);
      expect(routeMeta[route].description.length).toBeGreaterThan(20);
      expect(routeMeta[route].description.length).toBeLessThanOrEqual(170);
    }
  });

  it("wires every non-home page to its exact metadata record", () => {
    const problems: string[] = [];
    for (const route of routes) {
      const file = pageFile(route);
      if (!existsSync(file)) {
        problems.push(`missing page for ${route}`);
        continue;
      }
      const source = readFileSync(file, "utf8");
      if (!source.includes(`pageMetadata("${route}")`)) {
        problems.push(`${route} does not call its exact pageMetadata record`);
      }
    }
    expect(problems).toEqual([]);
  });

  it("gives every page complete canonical and social metadata", () => {
    for (const route of routes as RoutePath[]) {
      const page = pageMetadata(route);
      expect(page.title).toEqual({ absolute: routeMeta[route].title });
      expect(page.description).toBe(routeMeta[route].description);
      expect(page.alternates?.canonical).toBe(route);
      expect(page.openGraph).toMatchObject({
        title: routeMeta[route].title,
        description: routeMeta[route].description,
        url: absoluteUrl(route),
      });
      const twitterImage = Array.isArray(page.twitter?.images)
        ? page.twitter.images[0]
        : undefined;
      expect(twitterImage).toMatchObject({ url: site.ogImage, alt: site.ogImageAlt });
    }
  });

  it("keeps the old privacy address as a noindex page pointing at the policy", () => {
    expect(redirects).toEqual({ "/privacy-policy/": "/privacy/" });
    expect(movedMetadata.robots).toEqual({ index: false, follow: true });
    expect(movedMetadata.alternates?.canonical).toBe("/privacy/");
    const source = readFileSync(pageFile("privacy-policy"), "utf8");
    expect(source).toContain('httpEquiv="refresh"');
    expect(source).toContain("withBasePath(target)");
  });

  it("has no route directory outside the contract and its redirects", () => {
    const orphans = readdirSync(join(root, "app"), { withFileTypes: true })
      .filter((entry) => entry.isDirectory())
      .filter((entry) => existsSync(join(root, "app", entry.name, "page.tsx")))
      .map((entry) => `/${entry.name}/`)
      .filter((route) => !(route in routeMeta) && !(route in redirects));
    expect(orphans).toEqual([]);
    expect(existsSync(join(root, "app", "wishlist"))).toBe(false);
  });

  it("lists every route in the sitemap and points robots at it", () => {
    const entries = sitemap();
    expect(entries.map((entry) => entry.url)).toEqual(
      routes.map((route) => `${site.url}${route}`),
    );
    expect(entries.some((entry) => entry.url.includes("privacy-policy"))).toBe(false);
    expect(robots().sitemap).toBe(`${site.url}/sitemap.xml`);
    expect(robots().host).toBe(`${site.url}/`);
  });

  it("ranks the product pages above the legal ones in the sitemap", () => {
    const byUrl = new Map(sitemap().map((entry) => [entry.url, entry]));
    expect(byUrl.get(`${site.url}/`)?.priority).toBe(1);
    expect(byUrl.get(`${site.url}/product/`)?.priority).toBe(0.9);
    expect(byUrl.get(`${site.url}/privacy/`)?.priority).toBe(0.4);
    for (const route of ["/terms/", "/licenses/"]) {
      expect(byUrl.get(`${site.url}${route}`)?.priority).toBe(0.2);
    }
  });
});

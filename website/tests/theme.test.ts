// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { readFileSync, readdirSync, statSync } from "node:fs";
import { join, resolve } from "node:path";
import { describe, expect, it } from "vitest";

const root = process.cwd();
const css = readFileSync(resolve(root, "tailwind.css"), "utf8");
const layout = readFileSync(resolve(root, "app/layout.tsx"), "utf8");
const tokenPath = resolve(
  root,
  "../taffy-core/resources/tokens/tokens.json",
);
const generatedPath = resolve(
  root,
  "../taffy-core/resources/tokens/generated/css/taffy-color-tokens.css",
);
const generated = readFileSync(generatedPath, "utf8");

type ThemeName = "light" | "dark";
type TokenFile = {
  token_order: string[];
  themes: Record<ThemeName, Record<string, string>>;
};

const tokens = JSON.parse(readFileSync(tokenPath, "utf8")) as TokenFile;

function block(source: string, header: string): string {
  const start = source.indexOf(header);
  expect(start, header).toBeGreaterThan(-1);
  let depth = 0;
  for (let i = start; i < source.length; i += 1) {
    if (source[i] === "{") depth += 1;
    if (source[i] === "}") {
      depth -= 1;
      if (depth === 0) return source.slice(start, i + 1);
    }
  }
  throw new Error(`unterminated block: ${header}`);
}

function declarations(source: string): Map<string, string> {
  const found = new Map<string, string>();
  for (const match of source.matchAll(/(--[a-z0-9-]+)\s*:\s*([^;]+);/gi)) {
    found.set(match[1]!.toLowerCase(), match[2]!.trim().toLowerCase());
  }
  return found;
}

function kebab(name: string): string {
  return name.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`);
}

function argbToCss(value: string): string {
  const alpha = value.slice(0, 2).toLowerCase();
  const rgb = value.slice(2).toLowerCase();
  return `#${rgb}${alpha === "ff" ? "" : alpha}`;
}

function channel(value: number): number {
  const part = value / 255;
  return part <= 0.04045
    ? part / 12.92
    : ((part + 0.055) / 1.055) ** 2.4;
}

function luminance(hex: string): number {
  const rgb = Number.parseInt(hex.slice(1, 7), 16);
  return (
    0.2126 * channel((rgb >> 16) & 255) +
    0.7152 * channel((rgb >> 8) & 255) +
    0.0722 * channel(rgb & 255)
  );
}

function contrast(left: string, right: string): number {
  const [high, low] = [luminance(left), luminance(right)].sort(
    (a, b) => b - a,
  ) as [number, number];
  return (high + 0.05) / (low + 0.05);
}

function token(theme: ThemeName, name: string): string {
  return argbToCss(tokens.themes[theme][name]!);
}

function sourceFiles(directory: string): string[] {
  const out: string[] = [];
  const walk = (path: string) => {
    for (const entry of readdirSync(path)) {
      const full = join(path, entry);
      if (statSync(full).isDirectory()) walk(full);
      else if (/\.tsx?$/.test(entry)) out.push(full);
    }
  };
  walk(resolve(root, directory));
  return out;
}

describe("app theme parity", () => {
  it("imports the generated app projection and aliases its semantic tokens", () => {
    expect(css).toContain(
      '@import "../taffy-core/resources/tokens/generated/css/taffy-color-tokens.css";',
    );
    for (const [alias, appToken] of [
      ["--surface", "--taffy-surface"],
      ["--surface-raised", "--taffy-surface-raised"],
      ["--surface-sunken", "--taffy-surface-sunken"],
      ["--surface-sheet", "--taffy-surface-sheet"],
      ["--text-primary", "--taffy-text-primary"],
      ["--text-secondary", "--taffy-text-secondary"],
      ["--accent", "--taffy-accent"],
      ["--focus-ring", "--taffy-focus-ring"],
    ] as const) {
      expect(css).toContain(`${alias}: var(${appToken});`);
    }
  });

  it("keeps the generated CSS byte values aligned with tokens.json", () => {
    const light = declarations(block(generated, ":root,"));
    const dark = declarations(block(generated, '[data-taffy-theme="dark"]'));
    for (const theme of ["light", "dark"] as const) {
      const palette = theme === "light" ? light : dark;
      for (const name of tokens.token_order) {
        expect(palette.get(`--taffy-${kebab(name)}`), `${theme} ${name}`).toBe(
          argbToCss(tokens.themes[theme][name]!),
        );
      }
    }
  });

  it("does not copy app palette values into the website stylesheet", () => {
    for (const theme of ["light", "dark"] as const) {
      for (const name of tokens.token_order) {
        expect(css, `${theme} ${name}`).not.toContain(
          argbToCss(tokens.themes[theme][name]!),
        );
      }
    }
  });

  it("clears WCAG AA for the text pairs used by the site", () => {
    for (const theme of ["light", "dark"] as const) {
      for (const [ink, ground] of [
        ["textPrimary", "surface"],
        ["textPrimary", "surfaceRaised"],
        ["textSecondary", "surface"],
        ["textSecondary", "surfaceRaised"],
        ["textSecondary", "surfaceSunken"],
        ["accentOn", "accent"],
      ] as const) {
        expect(
          contrast(token(theme, ink), token(theme, ground)),
          `${theme}: ${ink} on ${ground}`,
        ).toBeGreaterThanOrEqual(4.5);
      }
      expect(
        contrast(token(theme, "focusRing"), token(theme, "surface")),
        `${theme}: visible focus ring`,
      ).toBeGreaterThanOrEqual(3);
    }
  });

  it("uses the app theme attribute and resolves it before first paint", () => {
    expect(css).toContain('[data-taffy-theme="dark"]');
    expect(layout).not.toContain('from "next/script"');
    expect(layout).toMatch(/<script[^>]*\bid="taffygo-theme-init"/);
    expect(layout).toContain("taffygo-theme");
    expect(layout).toContain("document.documentElement.dataset.taffyTheme");
    expect(layout).toContain("try{t=window.matchMedia");
    expect(layout).toContain("try{var s=window.localStorage");
    expect(css).toContain(":root:not([data-taffy-theme]) .lockup-dark");
  });

  it("gives the selected theme a high-contrast non-colour-only outline", () => {
    const switchSource = readFileSync(
      resolve(root, "components/ThemeSwitch.tsx"),
      "utf8",
    );
    expect(switchSource).toContain("border-2 border-primary");
    expect(switchSource).toContain("aria-pressed");
    for (const theme of ["light", "dark"] as const) {
      expect(
        contrast(token(theme, "textPrimary"), token(theme, "surfaceRaised")),
        `${theme}: selected-theme outline`,
      ).toBeGreaterThanOrEqual(3);
    }
  });

  it("derives browser chrome colours from the app token source", () => {
    const manifestSource = readFileSync(resolve(root, "app/manifest.ts"), "utf8");
    expect(layout).toContain('from "@/lib/app-theme"');
    expect(manifestSource).toContain('from "@/lib/app-theme"');
    for (const theme of ["light", "dark"] as const) {
      expect(layout).not.toContain(token(theme, "surface"));
      expect(manifestSource).not.toContain(token(theme, "surface"));
    }
  });

  it("paints no gradients anywhere", () => {
    expect(css).not.toMatch(/-gradient\(/);
    for (const file of [...sourceFiles("app"), ...sourceFiles("components")]) {
      expect(readFileSync(file, "utf8"), file).not.toMatch(/-gradient\(/);
    }
  });

  it("stills motion and smooth scrolling under reduced motion", () => {
    const reduced = block(css, "@media (prefers-reduced-motion: reduce)");
    expect(reduced).toContain("transition-duration: 0.001ms");
    expect(reduced).toContain("animation-duration: 0.001ms");
    expect(reduced).toContain("scroll-behavior: auto");
    expect(css).not.toContain("@keyframes");
  });

  it("clips sideways overflow at the root without making a scroll container", () => {
    const base = block(css, "@layer base {");
    expect(block(base, "html {")).toContain("overflow-x: clip");
    expect(block(base, "body {")).toContain("overflow-x: clip");
    expect(css).not.toMatch(/overflow-x:\s*hidden/);
  });

  it("sets headings upright and in two weights", () => {
    const base = block(css, "@layer base {");
    const headings = block(base, "h1,");
    expect(headings).toContain("font-style: normal");
    expect(headings).toContain("font-weight: 700");
    expect(headings).toContain("overflow-wrap: anywhere");
    expect(css).not.toMatch(/font-style:\s*italic/);
  });

  it("self-hosts the typeface through next/font and asks no other host for assets", () => {
    expect(layout).toContain('from "next/font/local"');
    expect(layout).toContain('src: "../public/fonts/space-grotesk-latin-variable.woff2"');
    expect(layout).toContain('variable: "--font-grotesk"');
    expect(layout).toContain("${grotesk.variable} ${instrument.variable}");
    expect(layout).toContain('src: "../public/fonts/instrument-serif-latin.woff2"');
    expect(css).toContain("--font-sans: var(--font-grotesk)");
    expect(css).not.toContain("@font-face");
    expect(css).not.toMatch(/url\(/);
    for (const file of [...sourceFiles("app"), ...sourceFiles("components")]) {
      expect(readFileSync(file, "utf8"), file).not.toMatch(
        /(src|srcSet)=["'{`]?https?:\/\//,
      );
    }
  });

  it("keeps client JavaScript limited to interactive surfaces", () => {
    const clients = [...sourceFiles("app"), ...sourceFiles("components")]
      .filter((file) => readFileSync(file, "utf8").includes('"use client"'))
      .map((file) => file.split("/").pop())
      .sort();
    expect(clients).toEqual(["BrowserWorkbench.tsx", "EnginePresentation.tsx", "Header.tsx", "LocalToolsPreview.tsx", "MotionControl.tsx", "NewTabPreview.tsx", "PageAssistantPreview.tsx", "ProviderDirectory.tsx", "ServiceWorkerRegistration.tsx", "ThemeSwitch.tsx", "WorkflowDemo.tsx"]);
  });
});

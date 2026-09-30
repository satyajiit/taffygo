// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// @vitest-environment node
import { createHash } from "node:crypto";
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { basename, join } from "node:path";
import { describe, expect, it } from "vitest";
import { SCREEN_SIZES, screens, type ScreenId } from "@/lib/content/screens";
import { site } from "@/lib/site";

const root = process.cwd();
const record = readFileSync(join(root, "taffy-generated-assets.txt"), "utf8");

function sha256(path: string): string {
  return createHash("sha256").update(readFileSync(join(root, path))).digest("hex");
}

/** Pixel size from a WebP header (lossy, lossless or extended). */
function webpSize(bytes: Buffer): [number, number] {
  const chunk = bytes.toString("ascii", 12, 16);
  if (chunk === "VP8 ") {
    return [bytes.readUInt16LE(26) & 0x3fff, bytes.readUInt16LE(28) & 0x3fff];
  }
  if (chunk === "VP8L") {
    const bits = bytes.readUInt32LE(21);
    return [(bits & 0x3fff) + 1, ((bits >> 14) & 0x3fff) + 1];
  }
  if (chunk === "VP8X") {
    return [bytes.readUIntLE(24, 3) + 1, bytes.readUIntLE(27, 3) + 1];
  }
  throw new Error(`not a WebP file: ${chunk}`);
}

/** Pixel size from the first start-of-frame marker of a JPEG. */
function jpegSize(bytes: Buffer): [number, number] {
  let offset = 2;
  while (offset < bytes.length) {
    const marker = bytes[offset + 1]!;
    const length = bytes.readUInt16BE(offset + 2);
    if (marker >= 0xc0 && marker <= 0xcf && ![0xc4, 0xc8, 0xcc].includes(marker)) {
      return [bytes.readUInt16BE(offset + 7), bytes.readUInt16BE(offset + 5)];
    }
    offset += 2 + length;
  }
  throw new Error("no start-of-frame marker");
}

function size(path: string): [number, number] {
  const bytes = readFileSync(join(root, path));
  return path.endsWith(".jpg") ? jpegSize(bytes) : webpSize(bytes);
}

/** Every image this site made for itself: screens, paintings and the card. */
function generatedFiles(): string[] {
  const list = (directory: string) =>
    readdirSync(join(root, directory))
      .filter((name) => !name.startsWith("."))
      .map((name) => `${directory}/${name}`);
  return [
    ...list("public/screens"),
    ...list("public/art"),
    ...readdirSync(join(root, "public"))
      .filter((name) => name.startsWith("og-image"))
      .map((name) => `public/${name}`),
  ];
}

function entryFor(path: string): Map<string, string> {
  const start = record.indexOf(`path=website/${path}\n`);
  expect(start, `no record entry for ${path}`).toBeGreaterThan(-1);
  const block = record.slice(start).split("\n\n")[0]!;
  return new Map(
    block.split("\n").map((line) => {
      const at = line.indexOf("=");
      return [line.slice(0, at), line.slice(at + 1)] as const;
    }),
  );
}

describe("generated website images", () => {
  it("binds every screen, painting and card on disk to its record entry", () => {
    const files = generatedFiles();
    expect(files.length).toBe(Object.keys(screens).length * 2 + 6 + 1);
    for (const path of files) {
      const entry = entryFor(path);
      const [width, height] = size(path);
      expect(entry.get("sha256"), path).toBe(sha256(path));
      expect(entry.get("bytes"), path).toBe(String(statSync(join(root, path)).size));
      expect(entry.get("dimensions"), path).toBe(`${width}x${height}`);
      expect(entry.get("source_sha256"), path).toMatch(/^[0-9a-f]{64}$/);
    }
  });

  it("has a record entry for no file that is missing", () => {
    const files = new Set(generatedFiles().map((path) => `website/${path}`));
    const recorded = [...record.matchAll(/^path=(website\/public\/\S+)$/gm)].map(
      (match) => match[1]!,
    );
    for (const path of recorded) {
      expect(files.has(path), path).toBe(true);
    }
  });

  it("serves every screen at both widths and at the phone's aspect", () => {
    for (const id of Object.keys(screens) as ScreenId[]) {
      for (const expected of SCREEN_SIZES) {
        const path = `public/screens/${id}-${expected.width}.webp`;
        expect(existsSync(join(root, path)), path).toBe(true);
        expect(size(path), path).toEqual([expected.width, expected.height]);
      }
    }
  });

  it("uses a 1200x630 card whose name carries its hash", () => {
    const path = `public${site.ogImage}`;
    expect(size(path)).toEqual([1200, 630]);
    expect(basename(path)).toContain(sha256(path).slice(0, 12));
    const studio = JSON.parse(readFileSync(join(root, "design/asset-manifest.json"), "utf8"));
    expect(studio).toContainEqual(expect.objectContaining({ path, sha256: sha256(path), tool: "HTML capture + built-in image_gen assets" }));
  });

  it("ships no retired images", () => {
    for (const retired of [
      "public/og-image-c963d90b6c24.png",
      "public/characters",
      "public/og-image.png",
    ]) {
      expect(existsSync(join(root, retired)), retired).toBe(false);
    }
  });
});

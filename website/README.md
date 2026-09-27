# TaffyGo website

The site at `https://taffygo.com`: eight pages about TaffyGo 1.0, exported
as plain static files and served by GitHub Pages. There is no server behind
it, it sets no cookies, and it makes no request to any other host.

## What is on it

| Route | What it says |
|---|---|
| `/` | What the app does, on screens captured from a phone, and how to install it |
| `/product/` | One errand from the request to the result, in three screens |
| `/built-for-phones/` | The bottom row, Taffy's tabs kept apart from yours, light and dark |
| `/contact/` | Issues, discussions and private security reports, all on GitHub |
| `/privacy/` | What stays on the phone, what goes to websites and the AI provider you connect, and what reaches us: nothing |
| `/delete-my-data/` | There is nothing on our side to delete, and how to clear the phone |
| `/terms/` | The Mozilla Public License 2.0, no warranty, and the services you connect |
| `/licenses/` | Where the notices for the app and for this site are |

`/privacy-policy/` is an old address. It is a static page that sends the
browser on to `/privacy/` and asks search engines not to index it, because
GitHub Pages has no server redirects.

Every word on the site lives in [`lib/content.ts`](lib/content.ts) (header,
footer, install links) and one module per route under
[`lib/content/`](lib/content). The tests read those modules as a whole, so a
banned word, a stale claim or a link to nowhere fails before it is built.

## Working on it

Dependencies install from the repository root, which holds the lockfile and
the workspace file:

```bash
pnpm install --frozen-lockfile
```

Then, from the repository root:

```bash
pnpm --dir website dev        # local development server
pnpm --dir website lint       # ESLint
pnpm --dir website typecheck  # tsc --noEmit
pnpm --dir website test       # Vitest: copy, pages, metadata, images, theme
pnpm --dir website build      # static export into website/out/
pnpm --dir website run audit  # Lighthouse on the export (build first)
```

Run `lint`, `typecheck` and `test` as three commands: pnpm passes trailing
words to the script as arguments rather than running them as further scripts.

`audit` serves `website/out/` the way GitHub Pages does (gzip for text, under
the same base path the build used) and holds every page to 1.00 in
accessibility and at least 0.95 in performance, best practices and SEO on the
mobile profile. With no Chrome on the host it says it skipped and reports no
score.

## The base path

The same export has to work at two addresses: `https://taffygo.com/` and
`https://satyajiit.github.io/taffygo/`. The build reads
`NEXT_PUBLIC_BASE_PATH` (empty, or `/taffygo`), and
[`lib/base-path.ts`](lib/base-path.ts) is the only place that interprets it.
`next.config.ts` passes it to Next.js as `basePath` and `assetPrefix`, and
every path to a file in `public/` or to another page goes through
`withBasePath()` or `siteHref()`. The site uses plain `<a>` elements, so
nothing adds the prefix for you; a test fails on any `src`, `srcSet` or
`href` in `app/` or `components/` that starts with `/` without the helper.

```bash
NEXT_PUBLIC_BASE_PATH= pnpm --dir website build          # for taffygo.com
NEXT_PUBLIC_BASE_PATH=/taffygo pnpm --dir website build  # for github.io/taffygo
```

Canonical links, Open Graph URLs, the sitemap and structured data always
name `https://taffygo.com`, whichever address the build is for.

## Deploying

`.github/workflows/pages.yml` builds the site and publishes it to GitHub
Pages on every push to `main` that touches `website/`, and can be started by
hand. It takes the base path from the repository variable `SITE_BASE_PATH`:
unset means `/taffygo`, and `/` means the root of `taffygo.com`. The workflow
runs no tests, so run the three commands above before you push.

## Images

- **Phone screens** in `public/screens/` are captures of the app on an
  Android phone, with the status and navigation bars cut off. Each is served
  at 360 and 720 pixels wide. [`PhoneFrame`](components/PhoneFrame.tsx) shows
  one inside a rounded hairline border, with numbered pins that the list
  beside it explains; nothing draws a device around it.
- **Paintings** of Taffy in `public/art/` and the link-preview card
  `public/og-image-<hash>.jpg`.
- [`taffy-generated-assets.txt`](taffy-generated-assets.txt) records where
  each of those came from, with its size, byte count and sha256, and
  `tests/assets.test.ts` holds the files to it.
  [`scripts/build_media.py`](scripts/build_media.py) makes them again from
  the capture and painting sources.
- Logos, icons, the typeface subset and the official Google Play badge are
  made by [`scripts/build_assets.py`](scripts/build_assets.py) from the
  masters in [`brand/`](../brand/README.md). The badge is Google's own file
  and is never redrawn.

The paintings, the card and the logos show the TaffyGo name, logo or the
Taffy character, which the source licence does not cover; see
[`TRADEMARKS.md`](../TRADEMARKS.md).

## Design

- **Colour** comes from the app. `tailwind.css` imports the CSS generated
  from `taffy-core/resources/tokens/tokens.json` and maps those tokens to the
  site's own names, so the site carries no palette of its own. A test fails if
  a colour value from the app's token file is copied into the stylesheet, and
  checks the contrast of every text pair the site uses.
- **Theme** follows the phone or computer until you choose one with the sun
  and moon buttons in the header, which store the choice in this browser. An
  inline script sets it before the first paint.
- **Type** is Space Grotesk, served from this site by `next/font/local` from
  [`public/fonts/`](public/fonts), with its licence beside it. Headings are
  upright, in two weights.
- **Script.** The theme buttons are the only client component. The questions
  use native disclosure elements, so every page reads the same with
  JavaScript off.
- **Motion** is limited to colour changes and a one-pixel press, and is
  switched off under `prefers-reduced-motion`.
- **Layout.** No page scrolls sideways at any width from 320 pixels up, and
  no button or menu link wraps onto a second line.

## Layout

```text
website/
  app/                 the eight routes, the /privacy-policy/ redirect page,
                       sitemap, robots, manifest, icons, not-found
  components/          Header, Footer, ThemeSwitch, PhoneFrame, ScreenStop,
                       PageHead and Painting, PolicyPage, StoreLinks, home/
  lib/base-path.ts     the base path, withBasePath() and siteHref()
  lib/content.ts       header, footer and install links
  lib/content/         one copy module per route, and screens.ts
  lib/site.ts          name, canonical origin, GitHub links, route metadata
  tailwind.css         token aliases, type and space scale, components
  public/              screens/, art/, brand/, fonts/, licenses/, the card,
                       llms.txt and llms-full.txt
  scripts/             image and asset generation, the Lighthouse audit
  tests/               copy, pages, metadata, base path, images and theme
```

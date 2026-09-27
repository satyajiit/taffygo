# TaffyGo website

The site at `https://taffygo.com`: eleven pages about TaffyGo 1.0, exported
as plain static files and served by GitHub Pages. There is no server behind
it, it sets no cookies, and it makes no request to any other host.

## What is on it

| Route | What it says |
|---|---|
| `/` | What the app does, on screens captured from a phone, and how to install it |
| `/product/` | One errand from the request to the result, in three screens |
| `/built-for-phones/` | The bottom row, Taffy's tabs kept apart from yours, light and dark |
| `/use-cases/` | Timed document and banking demos, with local image resizing and explicit handovers |
| `/providers/` | All catalog providers and models, searchable by name and capability |
| `/technology/` | Chromium, native Android, the Rust service, and embedded Python |
| `/contact/` | Issues, discussions and private security reports, all on GitHub |
| `/privacy/` | What stays on the phone, what goes to websites and the AI provider you connect, and what reaches us: nothing |
| `/delete-my-data/` | There is nothing on our side to delete, and how to clear the phone |
| `/terms/` | The Mozilla Public License 2.0, no warranty, and the services you connect |
| `/licenses/` | Where the notices for the app and for this site are |

`/privacy-policy/` is an old address. It is a static page that sends the
browser on to `/privacy/` and asks search engines not to index it, because
GitHub Pages has no server redirects.

Shared navigation and installation copy lives in `lib/content.ts`. Product copy
lives in `lib/content/` and the route components. `lib/simulation.ts` owns the timed demo state machine; verification and approval are gates. `lib/prepare-demo-photo.ts` resizes the sample image locally, with no external requests.
The provider directory is generated from the browser catalog; regenerate it with
`python3 website/scripts/build_provider_directory.py`. The matching `--check`
refuses drift.

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

`design.md` defines the Hallmark system: floating sticky navigation, an interactive recreation of the app’s idle new-tab UI, transparent Taffy artwork, software layers, and a compact Matterward Labs footer. App light and dark colors remain the base. Space Grotesk and Instrument Serif are self-hosted.

`experience.css`, `simulation.css`, `directory.css`, and `product-previews.css` extend the existing Tailwind entry. Generated foreground assets work in both themes. `design/cutout-prompts.json` records their imagegen prompts and paths. Full-resolution sources stay in `design/sources/`; responsive WebP versions are served from `public/cutouts/`.

The task demos navigate timed HTML states, pause at verification or approval, and support replay. The banking demo resizes a fictional portrait from `public/demo/sample-photo.png` using browser canvas. Its measured output is shown in the UI. A sample PDF can be downloaded from the document demo. Neither demo contacts a bank or government service.

The hero combines the app’s original time-of-day scene artwork with coded address-box, frequent-site, and dock controls. Separate coded previews demonstrate tabs, Library, blocking, backups, page questions, document and spreadsheet tools, and AI connections. Sample data is labeled. Floating artwork, CSS perspective, scroll reveals, and a pause control provide motion. Reduced motion removes decorative animation and automatic demo playback. The provider directory supports search, combined capability filters, availability, and sorting. All model records are present in the initial HTML.

## Offline access and updates

Production builds generate `out/sw.js` with a content-based version. It precaches the home page, offline notice, and immutable Next.js bundles. Visited pages and images use a bounded local cache. Navigation tries the network first and falls back to a saved page or the offline notice. Cross-origin requests, submissions, and model catalog data are not intercepted.

Registration waits until the page loads and is disabled in development. Updates bypass the HTTP cache and are checked on return to the tab, reconnection, and every 30 minutes. A successfully cached update activates immediately and refreshes open pages once. A failed installation leaves the current worker in place. Old site caches are removed after activation; unrelated caches are retained.

## Search and AI discovery

Every public route has its own title, description, canonical, Markdown alternate, and social metadata. The sitemap lists all eleven pages. JSON-LD identifies the app and publisher; FAQ data matches visible answers.

The build runs `scripts/export-markdown.mjs` after Next.js exports HTML. It generates a Markdown counterpart for every canonical page directly from that HTML, plus `markdown-index.json`, `providers.json`, `llms.txt`, and `llms-full.txt`. Outputs are written to both `out/` and `public/`, so development and production links work. Do not edit generated Markdown manually.

`robots.txt` permits all crawlers. `ai-policy.txt` and `.well-known/ai-policy.json` explicitly permit crawling, indexing, retrieval, summarization, and training on all first-party public site content. Third-party materials keep their respective terms. No JavaScript or server is required to read the content or model catalog.

## Layout

```text
website/
  app/                 the eleven routes, the /privacy-policy/ redirect page,
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

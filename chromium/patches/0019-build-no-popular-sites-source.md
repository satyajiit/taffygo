# 0019 — Build no popular-sites source for the NTP tiles

**Status:** `[Current]` — applied 2026-08-18, found by the first egress
capture (decision 0019's validation step)
**Needed by:** decision 0019 — a fresh profile's first launch fetched a
Google-curated site list from gstatic and then fanned out icon requests to
every listed site (Facebook, Amazon, Instagram, Wikipedia and more), none
of which the user asked for
**Estimated size:** ~6 modified upstream lines, 1 file

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/ntp_tiles/chrome_popular_sites_factory.cc` |
| Symbol | `ChromePopularSitesFactory::NewForProfile` |

## The change

Return null instead of constructing `PopularSitesImpl`.
`MostVisitedSites` treats a null popular-sites source as "none" by
contract (it null-checks before every use), so the user's own most-visited
tiles keep working and the default-tile slots simply stay empty until there
is history to fill them.

## Why the overlay cannot host it

The factory is the single construction point, in an upstream file. There is
no feature or pref that stops the fetch: `kPopularSitesBakedInContentFeature`
only selects the compiled-in placeholder list, and the fetch plus the
per-site icon cacher run whenever a `PopularSites` instance exists and the
grid has open slots.

## Rebase risk

**Low.** The function body is three lines; if upstream restructures the
factory the patch conflicts loudly. The behavior contract relied on — a
null source is legal — is exercised by upstream's own tests.

**Retirement:** with TaffyGo's own new-tab surface, which replaces the
upstream NTP tiles wholesale; the patch dies with the surface.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit chrome_popular_sites_factory.cc in the checkout; commit with the
# Taffy-Patch trailer
./tools/chromium/export-patches
./tools/check fast
```

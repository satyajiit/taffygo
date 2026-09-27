# TaffyGo trademarks and reserved media

TaffyGo's source code is published under the Mozilla Public License, version
2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE). That licence grants no
trademark rights, and this file grants none either.

Two separate things are reserved here, and the reason is the same for both: a
person who installs something called TaffyGo, with Taffy in it, should be
getting the product this repository builds. Copyright permission does not
carry permission to be us.

## The names and the marks

"TaffyGo" and "Taffy" are the product and assistant names, and the marks,
logos, lockups and trade dress are reserved by Matterward Labs Private
Limited. The mark files themselves, and the rules for using them, are under
[`brand/`](brand/README.md).

## The reserved media

These files are copyrighted work that ships with the product. They are
reserved rather than licensed with the code, and each path below is exact, so
a fork can find and remove them mechanically:

| What | Where | Count |
|---|---|---|
| The brand marks, the Taffy character master and its poses | `brand/` | 46 files |
| The launcher icon, splash icons and in-app marks made from `brand/` | the files listed in `taffy-core/ui/android/tools/icons/checksums.sha256`, under `taffy-core/ui/android/app/src/main/res/` and `taffy-core/ui/android/core/ui/src/main/res/` | 40 |
| The website's copies of the marks | `website/public/brand/taffygo-*.webp`, and `website/app/icon.png`, `website/app/apple-icon.png` and `website/app/favicon.ico` | 16 files, 3 icons |
| The onboarding films and their posters | `taffy-core/ui/android/feature/onboarding/src/main/res/raw/` and `.../res/drawable-nodpi/` | 8 films, 10 posters |
| The task scenes and their masters | `taffy-core/ui/android/core/ui/vendor/task-scenes/masters/` and the shipped copies under `taffy-core/ui/android/core/ui/src/main/res/drawable-nodpi/` | 2 masters |
| The start-page paintings | `taffy-core/ui/android/core/ui/vendor/start-scenes/webp/`, and the archive the app installs them from, `taffy-core/components/delivery/bundled/scenes-4x3-webp.zip` | 24, and 1 archive of the same 24 |
| The settings illustrations | `taffy-core/ui/android/feature/settings/vendor/masters/` and the built copies under `.../src/main/res/drawable-*/` | 4 masters |
| The profile pictures | `taffy-core/ui/android/core/ui/vendor/profile-banners/` and the shipped copies under `.../src/main/assets/profile-banners/` | 31 each |
| The website's paintings and its link-preview card | `website/public/art/` and `website/public/og-image-2655c535d406.jpg` | 6 files, 1 card |
| The repository banner and social preview | `.github/assets/banner.png` and `.github/social-preview.png` | 2 |
| The screenshots of the app | `website/public/screens/`, `.github/assets/screens/` and `tools/play.d/metadata/android/en-GB/images/phoneScreenshots/` | 26, 5 and 8 |
| The store icon and feature graphic | `tools/play.d/metadata/android/en-GB/images/icon.png` and `tools/play.d/metadata/android/en-GB/images/featureGraphic.png` | 2 |

## What a fork does

Take the code. Change the name, and remove or replace the files above. The
code that reads them is licensed to you like the rest of the product, so
pointing it at your own artwork is an ordinary change.

You may say plainly what your product is — "derived from TaffyGo", "a fork of
TaffyGo" — and you may keep the names inside the source where they are
identifiers rather than branding. What you may not do is publish a product
that presents itself as TaffyGo, or as endorsed by it.

## What this file does not cover

Third-party marks and artwork vendored into this repository belong to their
owners, and neither the licence nor this file grants anything over them. They
carry their own provenance records beside them, including the interface icons,
the search-engine icons and the provider marks under
`taffy-core/ui/android/`. The same is true of every mark reachable from the
browser it is built on: nothing here authorises use of the names of the
Chromium project, its contributors, or Google LLC to endorse or promote any
product.

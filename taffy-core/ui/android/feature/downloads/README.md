# `:feature:downloads`

**Status:** `[Current]` Screen SCR-203: the person's bounded profile download
history and TaffyGo's separately labelled required parts.

The feature owns one `DownloadRepository` interface. Production adapts the
exact regular Chromium profile's download store at that seam; host tests use an
in-memory adapter. Search, filtering, sorting and grouping are immutable
projections built on the injected processor dispatcher, never work repeated by
Compose rows. A failed or stalled initial store read is shown as unavailable;
live changes remain bounded and a late store reply can recover the screen.

The organizer exposes only actions Chromium advertises for the current item.
It does not invent retry, rename, move, tagging, paths, or file access. Private
downloads remain in their private profile and never enter the regular profile's
repository.

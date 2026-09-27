# `:core:analytics`

**Status:** `[Current]` A closed, content-free event vocabulary and a bounded
in-memory diagnostic sink.

`AnalyticsEvent` is sealed and carries no free text. Counts are bucketed. The
implementation sends nothing to a network and drops the oldest entry when its
bounded ring is full.

Regular Dagger installs `AnalyticsBindings` in the Profile component and obtains
the sink from `AnalyticsGraph`, keeping construction inside the module that owns
the internal implementation. Features receive only `AnalyticsClient`.

This module neither measures page content nor stores task, workspace, account,
or preference state. The privacy boundary is defined in
`docs/security/data-and-privacy.md`.

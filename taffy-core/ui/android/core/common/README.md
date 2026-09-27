# `:core:common`

**Status:** `[Current]` The three dispatchers, the clock, the logger, and the
total result type.

Owning milestone: M0, work package
WP-M0-07.

Authoritative specifications:
android-app-architecture.md
sections 5 and 9; decision
0014 item 5.

## The authority boundary

**This module supplies seams; it holds no state and makes no decisions.**
Everything it publishes exists so that something else can be replaced in a test:
the dispatcher a suspending call moves to, the clock a timestamp comes from, and
the sink a log line goes to. Nothing in TaffyGo-owned Kotlin names a global
dispatcher or a system clock directly, because a test that cannot replace those
is a test that cannot be deterministic.

`TaffyResult` is total: every boundary returns a success or a typed failure
rather than throwing, so a caller cannot forget the failure path and a screen
never has to parse a message to find out what went wrong.

## What this module deliberately does not do

It knows nothing about tasks, pages, workspaces, or screens. It has no
repository, no cache, and no persistence. It does not decide what a failure
means to a person — `FailureReason` is a closed vocabulary, and the module that
draws the failure owns the sentence.

## What each area owns

| Area | Owns |
|---|---|
| `AppDispatchers` | The three dispatchers, injected rather than referenced |
| `Clock` | The one source of the current time |
| `Logger` | The sink, and the levels that reach it |
| `TaffyResult` | Success, typed failure, and the closed failure vocabulary |
| `ApplicationScope` | The qualifier for the scope that outlives a screen |

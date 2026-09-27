# `:core:model`

**Status:** `[Current]` The vocabulary every other module in the UI host speaks.

Owning milestone: M0, work package
WP-M0-07. The screens
that use this vocabulary belong to milestones M1 to M3.

Authoritative specifications:
android-app-architecture.md
section 5; decision
0014;
ux-spec.md sections 4, 6 and 7 for the
words each type carries.

## The authority boundary

**This module describes; nothing here decides or performs.** It is a pure-JVM
module of immutable types and enumerations. It has no dependency on Android, on
Compose, on a repository, or on any other module in this build —
it is layer zero, and the module-graph check refuses any edge out of it.

Two rules are properties of the types rather than conventions a screen has to
remember:

- the seven user-visible task states are their own enumeration, and the thirteen
  durable ones map onto them through a total function, so a screen cannot show
  a state the task machine cannot reach;
- partial work has its own member. Nothing here can round it up to success.

## What this module deliberately does not do

It performs no work: no coroutines, no clock, no input or output, no formatting.
It carries no user-facing strings — a type names a state, and the module that
draws it owns the words. It holds no identifiers of Android resources.

## What each area owns

| Area | Owns |
|---|---|
| Task states | The seven displayed states, the mapping from the thirteen durable ones |
| Identity | The opaque value classes for workspaces, sources, facts, tabs, downloads |
| Research | Workspaces, sources, facts, fact kinds, timeline entries |
| Browsing | Tabs, downloads, download states, page-load failures |
| Preferences | Theme preference, app language, the country-to-language policy, provider route, model role, notification topic |
| Reading | The address-bar interpretation, which is a sealed hierarchy of readings |

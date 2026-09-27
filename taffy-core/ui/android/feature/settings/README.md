# `:feature:settings`

**Status:** `[Current]` Settings that lead somewhere: SCR-401, SCR-206,
SCR-406 and SCR-407.

Owning milestone: M3 (the assistant and workspaces) for the assistant
screens, and milestone M1 for appearance and notifications. Built here
under work package
WP-M0-07.

Authoritative specifications:
screen-catalog.md rows SCR-206,
SCR-401, SCR-406 and SCR-407;
ux-spec.md sections 9 and 12;
data-and-privacy.md section 6.

## The authority boundary

**A settings row that opens nothing is a promise the build has not kept.** The
settings home lists exactly the sections that have a screen behind them. It does
not advertise unfinished sections as disabled rows or as explanatory dead-end
copy. The account-optional introduction is informational rather than a control;
it uses the TaffyGo mark and promises no account destination.

**No provider surface lives here any more.** Screen SCR-404 and everything it
led to moved to [`:feature:providers`](../providers/README.md), together with
the credential and roster ports they read. A settings row still reaches SCR-404
through the navigation contract, which is the only thing this module knows
about it.

## What this feature deliberately does not do

It owns no storage implementation: ordinary choices go through
`:core:preferences`. It knows no other feature — a section row carries a
destination from the navigation contract, and the shell resolves it.

It offers no text-size control of its own. Text scaling is the system's setting
and the surfaces honour it to twice the default size; the appearance screen says
where to change it rather than adding a second place that could disagree.

## What each screen owns

| Screen | Catalog row | Owns |
|---|---|---|
| `SettingsHomeScreen` | SCR-401 | The identity card, search, and grouped list of sections that exist. The card draws this phone's profile — its picture and its name, or the screen's own name when none is set — and nothing else. On a tablet the list stays visible beside the open section |
| `YouScreen` | SCR-410 | The person hub: the profile hero, the chapters and further destinations as rows, and the profile details pane, which carries the name field and the face picker — the initials monogram first, then the thirty tiles. The field holds what was typed until Done or leaving the pane stores it, because the store trims and bounds what it is given and a field fed straight back from it could not be typed a two-word name. A row a private tab refuses dims and keeps its click |
| `TaffySettingsScreen` | SCR-405 | How Taffy talks, what Taffy can do and AI and providers as rows, and the suggestions-while-you-type switch |
| `PrivacyScreen` | SCR-403 | The hero, what is stored with its live inventory, the route and the retention, site settings, clear browsing data and what happened, and export and delete everything. It has no diagnostics switch: the one it had was read by nothing and TaffyGo has nowhere to send such facts (decision 0200) |
| `AboutScreen` | SCR-408 | The lockup, the app version, the Chromium version, the way to help, and two rows that open a tab: Licences, the engine's generated notice through `BrowserRepository.openAttributionNotice`, which takes no address, and Source code, the public repository (decision 0206) |
| `HelpScreen` | SCR-409 | The three limits and two ways to send feedback, both through something the person controls: an email draft their own email app opens, addressed to the one mailbox in `TaffyProjectContact`, and GitHub's new-issue page in a tab, said to be public before it opens (decision 0253). When no email app takes the draft the screen names the address. It has no diagnostics switch, for the reason the privacy row gives |
| `FilteringSettingsScreen` | SCR-206 | The blocking master toggle, the week and lifetime blocked totals, and the per-site exceptions with the way back. Every fact and command goes through the browser seam (decision 0076) |
| `NotificationsScreen` | SCR-406 | The topics the UI host may notify about |
| `AppearanceScreen` | SCR-407 | Theme, country-constrained language choices from `LanguageRegionPolicy`, the text-size sentence, and the pseudo-localized variant |
| `BrowserProfilesScreen` | SCR-708 | The browser's one profile: its name and its workspace count. No Add profile and no switch, because 1.0 ships one profile (decision 0255); a profile an earlier build left behind is listed as not in use and can be deleted, never opened. The name a person sees for the first profile comes from the shell, which replaces Chromium's "Your Chromium" |

Recorded drafts open their ordered review instead of an enabled switch.
`CoreSkillsRepository.acceptRecorded` sends the exact displayed definition
version, and the active publication remains the confirmation. An older
recording without review steps explains why it cannot be accepted. Built-in
and authored skill management retains its existing controls.

## Verification list

Ordered, and each item is a gap rather than a silent hole:

1. **The home lists what exists.** Ads and trackers, Taffy, Privacy,
   Appearance, Notifications, General, Profiles, and About and help are the
   eight home rows, and each one opens a screen in this module or one of
   `:feature:providers`'. A section whose screen does not exist is absent
   rather than shown as a row or discussed in dead-end copy; the note that
   once said "four sections, not seven" described an earlier inventory.
2. **Taffy's name is not a setting.** Avatar and Personality controls are
   absent until their cataloged implementation work rather than shown disabled.
3. **Nothing here reads a provider fact.** No source in this module names the
   roster, a credential port or a sign-in flow; `:core:credentials`,
   `:core:providerauth` and `:core:providers` are not dependencies of it. A
   provider control that reappeared here would fail the component graph rather
   than quietly duplicate `:feature:providers`.

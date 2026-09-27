// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_PREFS_H_
#define TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_PREFS_H_

class PrefRegistrySimple;

namespace taffy::filtering {

// The filtering plane's profile preferences (decision 0076). Declared beside
// the service that reads them rather than in the browser host, so the names
// and their reader cannot drift apart; the host's one registration function
// calls this one.

// Whether ad and tracker blocking is on for the profile. On by default —
// that is the product promise of CAP-BR-022, not a growth setting.
inline constexpr char kFilteringEnabledPref[] = "taffy.filtering.enabled";

// The hosts a person excepted from blocking, as a list of strings. Hosts,
// never locations, for the reason the browsing contract states.
inline constexpr char kFilteringSiteExceptionsPref[] =
    "taffy.filtering.site_exceptions";

// The lifetime count of blocked requests, flushed coarsely: at most every
// thirty seconds and at teardown. An undercount after a crash is acceptable;
// a preference write per request is not.
inline constexpr char kFilteringBlockedTotalPref[] =
    "taffy.filtering.blocked_total";

// The start of the current week window, as microseconds since the Windows
// epoch. Zero means no window has started. A window lasts seven days from
// this instant and then resets; private-tab blocks never write it.
inline constexpr char kFilteringWeekStartPref[] =
    "taffy.filtering.week_start";

// Blocked requests counted inside the current week window, flushed with
// the lifetime total. Never derived from kFilteringBlockedTotalPref.
inline constexpr char kFilteringBlockedThisWeekPref[] =
    "taffy.filtering.blocked_this_week";

// A bounded set of distinct document hosts that proves the displayed
// week-window lower bound. Once full, it stays a truthful minimum rather than
// retaining further identities. Hosts, never locations.
inline constexpr char kFilteringWeekSitesPref[] =
    "taffy.filtering.week_sites";

void RegisterFilteringPreferences(PrefRegistrySimple* registry);

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_PREFS_H_

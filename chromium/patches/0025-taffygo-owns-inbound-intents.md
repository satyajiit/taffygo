# 0025 — Point the inbound-intent alias at TaffyGo

**Status:** `[Current]` — committed on `taffy/patched` and exported by
`./tools/chromium/export-patches` on 2026-08-21 as
`0025-point-the-inbound-intent-alias-at-TaffyGo.patch`.
**Needed by:** WP-M1-04 — decision
0024 makes every
screen TaffyGo's. Patch [0022](0022-taffygo-launcher-activity.md) gave TaffyGo
the launcher tap and deliberately left every other door alone; this closes the
door most people actually use, which is tapping a link in another application
**Estimated size:** ~12 modified upstream lines, 1 file — one changed attribute
and one added `<activity>` block in `//chrome/android/java/AndroidManifest.xml`.
**Realised: 26 modified upstream lines** — the estimate was written before the
manifest was measured on a device, and it missed a second live door. See "What
the export changed".

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/android/java/AndroidManifest.xml` |
| Symbol | the `com.google.android.apps.chrome.IntentDispatcher` `activity-alias`, the `<application>` block that must declare its target, and `SearchActivity`'s `WEB_SEARCH` `<intent-filter>` |

## The defect this closes

Measured on the device of record on 2026-08-21, with
`/data/local/tmp/taffy-command-line` moved aside so that `--disable-fre` could
not answer the question for us:

```bash
adb shell am start -a android.intent.action.VIEW -d "https://example.net" com.taffygo.browser
```

reached `org.chromium.chrome.browser.firstrun.FirstRunActivity` on a profile
where TaffyGo's own onboarding had never run — "Welcome to Chrome", a Google
Terms of Service link, and a notice that Chrome sends usage and crash data to
Google — and `ChromeTabbedActivity` with Chrome's own toolbar (`url_bar`,
`home_button`, `tab_switcher_button`) on a profile where it had. That is the
repository owner's original complaint, and it is on the path a person is most
likely to take: a link tapped in another application.

The chain is entirely upstream's and TaffyGo is nowhere in it:

```text
alias com.google.android.apps.chrome.IntentDispatcher
  -> ChromeLauncherActivity.onCreate -> dispatch()
       -> FirstRunFlowSequencer.launch(this, intent)          # Chrome's first run
       -> LaunchIntentDispatcher.dispatchToTabbedActivity()   # Chrome's toolbar
            -> MultiWindowUtils.getTabbedActivityForIntent()  # ChromeTabbedActivity
```

`TaffyBrowserActivity.requiresFirstRunToBeCompleted` returns false, and that is
why the launcher tap is clean — but both of its call sites are inside
`AsyncInitializationActivity`, so the override only speaks for launches that
*start* at TaffyGo's activity. `ChromeLauncherActivity` is an `Activity`, not
an `AsyncInitializationActivity`, and asks `FirstRunFlowSequencer` directly.

## Why one alias is the whole of the surface

Every entry point in the manifest was enumerated before this change was shaped,
because the size of the list determines whether the fix is one patch or six.
Line numbers are `//chrome/android/java/AndroidManifest.xml` at the pin.

| Lines | Element | Target | Reachable how |
|---|---|---|---|
| 253–383 | `activity-alias com.google.android.apps.chrome.IntentDispatcher` | `ChromeLauncherActivity` | **18 intent filters** — this is the whole browse surface |
| 385–427 | `activity MediaLauncherActivity` | itself | `VIEW` on image and video MIME types |
| 454–482 | `activity-alias AudioLauncherActivity` | `MediaLauncherActivity` | `VIEW` on audio MIME types |
| 497–512 | `activity DragAndDropLauncherActivity` | itself | private action, `android:exported="false"` |
| 514–524 | `activity IncognitoTabLauncher` | itself | `…incognito.OPEN_PRIVATE_TAB` |
| 526–536 | `activity AutofillOptionsLauncher` | itself | `APPLICATION_PREFERENCES` |
| 599–639 | `activity ChromeTabbedActivity` | itself | exported, but its only filter is upstream's Daydream probe (`…dummy.action`); no `VIEW`, no `MAIN`/`LAUNCHER` |
| 641–648 | `activity TaffyBrowserActivity` | itself | not exported — patch 0022 added it |
| 649–668 | `activity-alias com.google.android.apps.chrome.Main` | `TaffyBrowserActivity` | `MAIN`/`LAUNCHER` — **already TaffyGo's**, patch 0022 |
| 803–816 | `activity WebappLauncherActivity` | itself | WebAPK start actions |
| 819–826 | `activity-alias SecureWebAppLauncher` | `WebappLauncherActivity` | not exported |
| 860–867 | `activity ActivateWebApkActivity` | itself | WebAPK activation |
| 927–941 | `activity ManageTrustedWebActivityDataActivity` | itself | Trusted Web Activity data management |
| 1060–1076 | `activity SearchActivity` | itself | `WEB_SEARCH` |
| 1300–1318 | `activity XrHostActivity` | itself | not exported |

The 18 filters on the first row are the ones a person or another application
reaches with a link or a share: `VIEW` for `http`, `https`, `about` and (on
this channel) `googlechrome`; the same set again with `text/html`,
`text/plain` and `application/xhtml+xml`; `SEND` with `text/plain`; `VIEW`
with `content:` and `file:` for HTML, MHTML, `message/rfc822` and web
bundles; `MEDIA_SEARCH`; `android.speech.action.VOICE_SEARCH_RESULTS`;
`NDEF_DISCOVERED` with `http`/`https`; `SEARCH`; and Samsung's
`com.sec.android.airview.HOVER`.

**So the whole browse surface is one `android:targetActivity` attribute — and
one filter that is not on it.** The link and share surface is the alias, which
is one attribute. `WEB_SEARCH` is not: it is an `<intent-filter>` on
`SearchActivity`, Chrome's own search surface, and it was measured on the
device rather than reasoned about —

```bash
adb shell am start -a android.intent.action.WEB_SEARCH --es query 'taffygo' com.taffygo.browser
```

put `org.chromium.chrome.browser.searchwidget.SearchActivity` on screen with
`url_bar`, `toolbar`, `location_bar_status` and `search_location_bar` in the
view tree. That is Chrome's toolbar on an entry path any application can
reach, so it is in this patch rather than named as a remainder: the filter
moves onto the alias TaffyGo owns, beside the two search filters already
there.

Both edits are in the same file, they are the same change — TaffyGo owns what
arrives from outside — and that is why this is one patch and not two.

Three things the enumeration establishes that are worth stating because a
reader will look for them and not find them:

- **There is no `intent://` filter at this pin.** `grep -n 'scheme="intent"'`
  over the manifest returns nothing; `IntentHandler` parses `intent:` URLs that
  arrive by other routes, but nothing advertises the scheme to the system.
- **There is no `PROCESS_TEXT`, `ASSIST`, `SEND_MULTIPLE` or app-link
  (`android:autoVerify`) filter either.** All four are things Chrome carries on
  other platforms or other manifests; none is in this one.
- **`ChromeTabbedActivity` remains exported and explicitly startable.** That
  is upstream's own manifest state and this patch does not touch it. While
  patch 0009's status island was mounted there (it has since retired), the
  explicit `am start -n …/ChromeTabbedActivity` route was how every
  semantic-graph observation on record was started. Nothing implicit resolves
  to it.

## Why this is a patch and not a downstream override

Asked first, every time, and answered against the file rather than from memory.

**A downstream jinja value would cost nothing, and there is none.** The alias
block carries `{% block common_view_intent_shared_filter_body %}` and two
sibling blocks, but they govern the *contents* of three filters, not the
alias's target. `android:targetActivity` on line 254 is a literal.

**A manifest merger cannot retarget an existing alias.** This is patch 0022's
finding and it still holds: merging adds and overlays elements, so a second
declaration of the same alias name would collide rather than replace, and a
second alias with the same filters would leave the intent resolving to two
activities. It was re-checked in the other direction as well —
`//taffy` contributes **no** `AndroidManifest.xml` at all today, so
there is no merged fragment to grow.

**And a merged fragment could not carry the target even if one existed.**
Android requires an `activity-alias` to name an activity declared *before* it
in the manifest; the merger appends library elements to `<application>`, which
is after. This is the same constraint patch 0022 recorded, and it is what
decides the shape of the change below.

## The change

Two edits in one file, immediately around the alias:

1. A TaffyGo activity declared directly above the alias, carrying
   `ChromeLauncherActivity`'s own dispatcher posture — `LauncherTheme` (which
   is `Theme.BrowserUI.NoDisplay`, so nothing is drawn), `taskAffinity=""`,
   `relinquishTaskIdentity`, `excludeFromRecents`, and the same
   `configChanges` list. Those attributes are copied deliberately: this
   activity finishes inside `onCreate` exactly as upstream's does, and drifting
   from that posture would change task behaviour for every caller.

2. The alias retargeted:

   ```xml
   android:targetActivity="org.chromium.taffy.shell.TaffyInboundIntentActivity"
   ```

3. The `WEB_SEARCH` filter moved from `SearchActivity` onto that alias, and a
   comment left in its place saying where it went.

`ChromeLauncherActivity` and `SearchActivity` both keep their declarations and
stay startable by explicit intent, so the rollback for this change is the same
shape as the rollback for 0022: put two attributes and one filter back.

## Why a TaffyGo activity above the alias, and not the alias pointed straight at `TaffyBrowserActivity`

The obvious cheaper change — one attribute, zero added lines — is to point the
alias at `TaffyBrowserActivity`, which already exists in this manifest. It is
not available, and the reason is the ordering rule above:
`TaffyBrowserActivity` is declared at line 641 and this alias is at line 253.
Making it work would mean moving patch 0022's block to the top of
`<application>`, which is a larger diff (ten lines deleted and re-added
somewhere else), which rewrites a patch that is already applied, and which
loses `{{ self.chrome_activity_common() }}` — that jinja block is *defined*
inside `ChromeTabbedActivity` at line 613, so a caller above line 253 would
have to spell the three attributes out and could then drift from what Chrome's
own activities carry.

The added activity is therefore not a convenience. It is the cheapest way to
satisfy a manifest ordering rule, and it earns its place twice over by being
the one point where TaffyGo decides what an inbound intent from an arbitrary
application *means* before any Chrome code sees it —
`TaffyInboundIntent`, a pure class with no Android and no native in it, checked
on a laptop by `TaffyInboundIntentTest`.

## What this deliberately does not do, stated so nothing is assumed closed

- **It does not touch `MediaLauncherActivity` or `AudioLauncherActivity`.**
  `VIEW` on an image, video or audio MIME type still opens Chrome's media
  viewer. Those are screens SCR-804 and SCR-805 and each is its own transfer;
  retargeting them here would point live filters at a surface that does not
  exist.
- **It does not touch the Custom Tab path.** An application that launches a
  `CustomTabsIntent` sets `EXTRA_SESSION`, and such an intent is now handled by
  TaffyGo's dispatcher, which has no Custom Tab of its own — so it opens the
  address as an ordinary TaffyGo tab rather than in Chrome's Custom Tab
  toolbar. That is a behaviour *change* for those callers and it is the correct
  one for this milestone: it means no third-party application can put Chrome's
  toolbar on screen. What it is not is a Custom Tab implementation, and nothing
  may describe TaffyGo as supporting the Custom Tabs protocol.
- **It does not touch the WebAPK entry points** (`WebappLauncherActivity`,
  `SecureWebAppLauncher`, `ActivateWebApkActivity`) or
  `ManageTrustedWebActivityDataActivity`. TaffyGo installs no web apps, so
  nothing can reach them, but they are not TaffyGo's and this patch does not
  claim them.
- **It takes `SearchActivity`'s filter but not `SearchActivity`.** The activity
  is Chrome's search-widget surface and keeps its declaration; nothing in
  TaffyGo starts it, and nothing implicit now resolves to it.
- **It does not touch `IncognitoTabLauncher` or `AutofillOptionsLauncher`.**
  Both are reachable only by an application that names Chrome's own private
  actions (`…incognito.OPEN_PRIVATE_TAB`, `APPLICATION_PREFERENCES`).
- **It does not read `android.speech.action.VOICE_SEARCH_RESULTS`.** That
  filter is on the alias and now reaches TaffyGo, but its payload is a bundle
  of parallel recognizer arrays rather than a query string, so TaffyGo opens
  and does nothing else with it.
- **It does not remove `ChromeLauncherActivity`.** Removing it would be a much
  larger patch against a class many upstream call sites name, and it would
  delete the fallback that makes the transfer safe to attempt.

## What TaffyGo does with the intent, and why the answer is "a new tab"

The upstream half of this change is one attribute. The behaviour is downstream,
costs no patch, and is worth stating here because a reader arriving at a rebase
needs to know what the attribute now reaches.

`TaffyInboundIntentActivity` extracts an address, builds a **fresh** intent
naming `TaffyBrowserActivity`, puts the address in one private extra, and
finishes. It never forwards the caller's intent object, which is what closes
the intent-redirection class of defect at the door rather than downstream of
it. The extraction rules are `TaffyInboundIntent`, and they are deliberately
string arithmetic with no `GURL` in them, because this activity runs before
native is loaded:

- `ACTION_SEND` with `text/plain` — scan `EXTRA_TEXT` for `http://` and then
  `https://` prefixed tokens and take the last, which is upstream's own rule in
  `IntentHandler.getUrlFromShareIntent`.
- anything else — the intent's data string.
- then an **allowlist**: `http`, `https`, `content`, `file`, and `about:blank`
  exactly. Everything else — `chrome:`, `javascript:`, `data:`, `intent:`,
  `googlechrome:`, `about:` anything-but-blank — is refused, and TaffyGo opens
  with no new tab. The allowlist is stricter than upstream's denylist and needs
  no native to evaluate, which is the whole reason it is an allowlist.

**A search is the one thing that leaves the dispatcher as words.** `WEB_SEARCH`,
`SEARCH` and `MEDIA_SEARCH` all carry `SearchManager.QUERY`, and whether those
words name a site or ask a question is not this path's decision to invent —
`AddressBarResolver` already makes it for everything typed into TaffyGo's
address bar. So the words cross in a second extra and `TaffyInboundQuery` hands
them to `BrowserRepository`, which either answers with a place to go or records
`BrowserNotice.NO_SEARCH_ENGINE`. That notice is where TaffyGo's honesty about
having chosen no search engine (OD-019) already lives, and screen SCR-101 puts
it in TaffyGo's own words. The repository is a process singleton and the notice
is a `StateFlow`, so a search that arrives before the browsing screen exists is
answered when it does — which is exactly the cold-start case.

`TaffyBrowserActivity` opens it **in a new tab, selected**, at
`TabLaunchType.FROM_EXTERNAL_APP`. Never the current tab: an inbound link from
another application must not destroy the page a person was already reading, and
TaffyGo has no trust signal here that would justify reuse (upstream reuses only
when `EXTRA_APP_ID` says the sender is the same application that opened the
current tab). Both a cold start and a warm one are handled — the cold path
through `startTabModel`, the warm path through
`AsyncInitializationActivity.onNewIntentWithNative` — and a launch that Android
replays from Recents or from a saved instance state is refused, so a link is
opened once and not once per recreation.

## Rebase risk

**Low, and for the same reason patch 0022's is.** The alias has carried the
same name and the same shape across milestones and this touches one attribute
of it plus a self-contained block above it.

The risk that matters is not conflict but silence, and it is sharper here than
for 0022. An upstream rename or split of `IntentDispatcher` would leave this
patch applying cleanly to a block nothing resolves to, and the symptom would be
Chrome's first run reappearing on a path nobody re-tests. **The standing check
is one command**, and it belongs in every milestone rebase:

```bash
adb shell cmd package resolve-activity -a android.intent.action.VIEW \
    -d https://example.net com.taffygo.browser
```

It must name `TaffyInboundIntentActivity`. Anything else is this defect again.

**Retirement: permanent, reviewed at each milestone rebase.** There is no
upstream to send this to — it is a downstream product decision about which
application owns a link — and it retires only if TaffyGo stops deriving from
`//chrome/android`'s manifest.

## What the export changed

The argument above survived contact with the file. The estimate did not, and
rule 7 of [the queue's README](README.md) says a specification records that
rather than being rewritten.

- **Twenty-six modified upstream lines, not twelve.** The estimate counted one
  retargeted attribute and one added block, and it was written before the
  `WEB_SEARCH` measurement below found a second live door. `git diff --stat`
  reports `20 insertions(+), 6 deletions(-)`, which is what the lane counts.
  The whole of it, so a reviewer can check the arithmetic rather than take it:

  | Edit | Added | Removed |
  |---|---|---|
  | The `TaffyInboundIntentActivity` block, and the three-line comment saying why it sits above the alias | 10 | 0 |
  | The alias's `android:targetActivity` | 1 | 1 |
  | The `WEB_SEARCH` filter on the alias, and its two-line comment | 6 | 0 |
  | `SearchActivity`'s filter, replaced by a three-line comment saying where it went | 3 | 5 |
  | **Total** | **20** | **6** |

- **Eight of the twenty-six are comment.** That is the honest cost of two
  positions that are load-bearing rather than editorial: an `activity-alias`
  may only name a target declared before it, and a reader who moves the block
  for tidiness produces a manifest that installs and an alias that resolves to
  nothing. The same cost patch 0022 paid, for the same reason, and it is in the
  patch rather than in this file only for the same reason.
- **The block is self-closing.** `<activity … />` rather than
  `<activity …></activity>`, which is one line fewer than upstream's own
  spelling for `ChromeLauncherActivity` and changes nothing else.

## Verification

- **Historical pre-hard-cut observation on the builder of record,
  2026-08-21.** This evidence explains the manifest patch but is not current
  proof for the `//taffy` product graph. The then-current
  `./tools/check fast --only chromium` moved from 21 realised patches and 216
  modified upstream lines to **22 and 242**, which is 55% and 16% of the budget
  of 40 and 1,500. `gn gen out/dev-arm64` printed
  `Done. Made 63379 targets from 4678 files in 2472ms`, and
  `autoninja -C out/dev-arm64` shell test target
  printed `The build has finished successfully`.
- **The rule that decides what a stranger's intent may do is checked on a
  laptop.** `out/dev-arm64/bin/run_taffy_shell_junit_tests` printed
  `[==========] 188 tests ran.` and `[  PASSED  ] 188 tests.`, of which 30 are
  `TaffyInboundIntentTest` — every allowed scheme, every refused one
  (`chrome:`, `javascript:`, `data:`, `intent:`, `googlechrome:`, every
  `about:` address except `blank`), the share-text ordering rule, the three
  search actions, and the voice-search action that carries no query.
- **Historical pre-hard-cut device observation, 2026-08-21**, with
  `/data/local/tmp/taffy-command-line` moved aside so `--disable-fre` could not
  answer the first-run question for us, and **put back afterwards** — same 117
  bytes, same contents. `pm clear com.taffygo.browser` first, so this is the
  profile on which the defect was worst.

  | Entry path | What happened |
  |---|---|
  | `VIEW https://example.net`, cold, cleared profile | `LaunchState: COLD`, `Activity: …/TaffyBrowserActivity`. On screen: TaffyGo's own onboarding — "A whole browser. Taffy only when you ask." Zero occurrences of `url_bar`, `home_button`, `tab_switcher_button` or `location_bar_status` in the view tree; zero occurrences of "Welcome to Chrome", "Terms of Service", "usage and crash data" or "Google". `dumpsys activity activities` counted **0** `FirstRunActivity`, `ChromeTabbedActivity`, `ChromeLauncherActivity` or `SearchActivity` in the stack. Through onboarding, the page arrived: "Example Domain", address bar `example.net`, TaffyGo's "Ask Taffy" bar |
  | `VIEW https://example.com`, warm | entered `TaffyInboundIntentActivity`, loaded `example.com`, no Chrome id in the tree |
  | `WEB_SEARCH --es query 'taffygo browser'` | `cmd package resolve-activity` names `TaffyInboundIntentActivity`. On screen: TaffyGo's own notice — "No search engine yet / TaffyGo hasn't chosen one, so nothing was sent — what you typed stayed on this phone." That is OD-019 in TaffyGo's words, where Chrome's `SearchActivity` toolbar used to be |
  | `SEND text/plain`, text `look at this https://example.org/page thanks` | `example.org` extracted by the share rule and loaded |
  | `VIEW about:version` | refused by the allowlist. `cr_TaffyInbound: no address TaffyGo will open; opening the browser`; TaffyGo came forward and opened **no** tab, leaving the page already open untouched |
  | `VIEW chrome://settings` | `Error: Activity not started, unable to resolve Intent` — the manifest advertises no `chrome` scheme, so the system refuses it before TaffyGo is asked |

- **The old in-browser runtime check is retired.** At the time, this path also
  proved that one in-browser runtime coordinator started and that no launch
  raised `FATAL EXCEPTION`. The hard cut removed that coordinator; current
  verification must instead prove that the per-profile utility service may
  terminate while the activity and manual browsing remain alive.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit chrome/android/java/AndroidManifest.xml in the checkout;
# commit with an owner and a reason, and the trailer `Taffy-Patch: 0025`
./tools/chromium/export-patches
./tools/check fast
```

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-08-21, from the
`Taffy-Patch: 0025` commit on `taffy/patched`. The hand-written `git diff`
export this section used to describe, and the debt it recorded — a queue
file whose lines existed only as uncommitted modifications in the checkout —
were retired the same day: the manifest edit was committed with its
trailer and the exporter rewrote the queue from it.
The branch and the queue agree, and `./tools/check fast --only chromium`
verifies that agreement together with the fork-debt figures.

# `:feature:onboarding`

**Status:** `[Current]` The first-run sequence and its one side door: SCR-001,
SCR-002, SCR-006, SCR-007 and SCR-004. Two screens are deleted rather than
unimplemented: SCR-710, the premium offer, because TaffyGo sells nothing, and
SCR-701, sign in, because there is no account to sign in to (decisions
0200 and
0201).
Neither id is reused.

Owning milestone: M3. Every screen here is now in the catalog's 0xx group —
the M8 surface this module used to carry was sign-in, and it left with it.
Built under work package
WP-M0-07.

Authoritative specifications:
screen-catalog.md rows SCR-001 to
SCR-007;
ux-spec.md;
and the design handoff, `handoff/DESIGN.md` and its screen mocks, which is
kept in the maintainer's working tree and not published.

## The authority boundary

**The sequence never takes hostages.** Get started asks two questions and
neither has to be answered: continuing with the field empty and the monogram
unchanged is a complete answer, not a postponed one. How Taffy thinks cannot
be left: a route must be chosen before the browser opens, or set up later
explicitly. Finishing replaces the back stack — setup is not history the
browser walks back into. "Start browsing" on the welcome screen means what it
says; Meet Taffy, Get started and AI setup are what this sequence puts between
a person and a browser.

**Only the last screen finishes the sequence.** SCR-004 records that the first
run is over, and it does so two ways: the person connects their own provider
and continues into key setup, or they set it up later and reach the browser
with no provider at all. The second is a real answer rather than a postponed
one — the browser is complete without a model — and it writes
`ProviderRoute.NOT_CONFIGURED` rather than leaving an earlier choice standing.
The primary action stays disabled until a route is chosen; the skip never
borrows it. Every earlier
screen only navigates, so a process death mid-sequence restores where the
user was rather than dropping them into the browser.

**Get started asks for nothing a server would hold.** A name and a picture,
both optional, both written to this phone and read by nothing else. There is
no credential, no validation and no refusal state on the screen, because there
is nothing for it to be refused by. The one rule it enforces it enforces
visibly: the name field stops taking characters at the bound the store keeps,
rather than accepting more and truncating on save.

**No screen here takes a provider key.** AI setup chooses a route and hands
detail to the providers screen (SCR-404), which owns that boundary.

## What this feature deliberately does not do

It stores no truth of its own: the finished flag, country, app language, theme
and provider route all live in the preference repository, and the name and
picture Get started collects go to the app-wide `taffy_you_profile` store
through `LocalProfileRepository` — the same store You reads and writes, so
there is one profile rather than a first-run copy of one. It knows no other feature — every hand-off is a
destination from the navigation contract, resolved by the shell.

It does not prompt for the default-browser role (SCR-005). The trusted Android
adapter offers that system sheet only after the core confirms an explicit
**Keep** action on a source-verifiable task file; first run has neither the
value nor the authority to ask.

The first-run pages add no stepper, category chip, or other app-drawn status-bar
treatment. Android owns the status bar; each page starts beneath its inset.

## What each screen owns

| Screen | Catalog row | Owns |
|---|---|---|
| `WelcomeScreen` | SCR-001 | The promise, "Start browsing", centered locale chip, and sunrise/moon light/dark choice; the shared theme wraps a full-window transition around either change |
| `MeetTaffyScreen` | SCR-002 | The Taffy mark, local sound control, and four theme-matched films of Taffy working beside a page |
| `LanguageRegionScreen` | SCR-006 | The handoff 12B grouping, complete country picker, searchable app languages, and sticky Done action |
| `GetStartedScreen` | SCR-007 | A name and a picture, both optional: the live initials monogram or one of the thirty bundled tiles, a bounded name field, three promises, and a "How TaffyGo handles your data" sheet carrying the version line |
| `AiSetupScreen` | SCR-004 | One route — the person's own provider — the own-key hand-off, "Set up later", and the finished flag |

One shape is shared and lives here rather than in `:core:ui`, because it is
the shape of an explanation and nothing outside the sequence explains anything
this way: `OnboardingSequence`, a scrolling body and an optional pinned
footer. There were two — `OnboardingCard` (glyph, title, sentence) left with
SCR-710, which was its only caller.

SCR-002 keeps the Taffy lockup and feature title above the film, and keeps the
page dots, prompt and forward action below it, as native Compose. Each 4:5 film
is borderless and fitted without cropping. The source canvas and lossless
posters use the exact `TaffyTheme.colors.surface` value. H.264 can move a flat
RGB edge by a few channel values during YUV decoding, so a short native-surface
feather covers all four decoded edges instead of trusting one decoder's colour
rounding. The film therefore dissolves into the page rather than looking like
a card or simulated window. Page dots stay native and are never baked into a
film as a stepper. Only the settled page owns a `ShowcaseVideoView` platform
decoder. It plays once and holds the result instead of looping before the
carousel advances; prefetched pages use the shared first-frame surface poster.
Reduced-motion mode keeps the matching final result visible and, when sound is
enabled, runs only the settled decoder invisibly so narration still plays. A 48 dp speaker
button sits at the top right without changing the header's height. Normal
ringer mode seeds sound on; silent and vibrate seed it off. That local choice
survives a configuration change and never changes the device ringer. Audible
playback asks for transient media focus. A temporary focus loss mutes the film
without changing the user's saved choice, and a permanent loss stays muted for
that film; completion, pause and disposal release focus. Every action that changes
or shares something asks for approval in the film; none presents Taffy as
blocked.

The packaged files come in light and dark pairs. Films live in `res/raw` and
posters in `res/drawable-nodpi`, using these four stems: `form`,
`weekly_review`, `job` and `page_tools`. A light film is named, for example,
`taffy_showcase_form_light.mp4`; its completed poster is
`taffy_showcase_form_light_poster.webp`. Motion-enabled pages begin on the
shared `taffy_showcase_start_light_poster.webp`. The other theme replaces
`light` with `dark`. `vendor/showcase-films.txt` and
`vendor/showcase-film-posters.txt` hold one checksum and one byte count per
file, checked in both directions by the `files` lane.

## Verification list

Ordered, and each item is a gap rather than a silent hole:

1. **Country selection is platform-backed, and the app language shows what is
   stored.** SCR-006 lists every ISO country Android exposes and stores the
   selected code through `UserPreferencesRepository`. Country and language are
   independent: changing country does not wipe a still-valid language. Country
   recognition is rendered locally from the ISO code; opening or scrolling the
   picker makes no country-specific network request. English, Hindi, and Follow
   the system are offered in every country. SYSTEM means follow the device when
   that catalogue exists, otherwise English. SCR-407 reads the same
   `LanguageRegionPolicy`; neither screen keeps a second country-to-language
   list.
2. **What happens to a person's data is stated in the product, not linked to.**
   Get started carries a "How TaffyGo handles your data" sheet rather than the
   two website links the sign-in screen's legal line carried. Those links —
   `taffygo.web.app/terms/` and `/privacy-policy/` — were the only outbound
   website addresses the application held, and this repository neither deploys
   nor controls that site. A sheet is also the only answer available at the
   moment it is asked: the screen is reachable with no network.
3. **There is no first-run step chrome.** The sequence has no step dots,
   category chip, or reserved top band. The country-and-language chip on
   Welcome remains a destination control, not sequence progress.
4. **Get started leads with the mark, not the lockup.** The icon generator
   derives the mark at five densities; the lockup the mock uses is not derived
   yet.
5. **Three of the seven rendered films are not here, and the carousel is four
   pages rather than seven.** `compare`, `driving-licence` and `mail`
   composite official Amazon, Flipkart, Croma, Reliance Digital, Samsung,
   DigiLocker, Ministry of Road Transport and Highways and Gmail artwork with
   no recorded permission from any of those owners, which is
   OD-094. The four here carry no
   third-party logo or product artwork at all, which is why they ship while
   the other three do not. Getting the other three back is a permission
   recorded owner by owner, not an engineering task: the authoring project
   that could re-render them without the marks was ignored by Git and is not
   on any host, so their bytes at `cff4504c^` are the only copies. Mixing the
   four films with code-drawn illustrations for the other three was rejected —
   the step between a 30 fps film and a pulsing halo reads as three broken
   slides.

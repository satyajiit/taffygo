# `:feature:browsing`

**Status:** `[Current]` The preserved Compose browser surfaces for SCR-101,
SCR-102, SCR-103, SCR-104, and SCR-108, the Ask overlay of SCR-301, plus the
ad- and tracker-blocking site sheet of SCR-204 — the blocking half of that
catalog row; the connection, permission and cookie panels remain M4 scope.

SCR-301 lives here because its composer is the start page's: `AskOverlay`
draws the scrim and the card, `AskComposer` hosts the start page's
`AddressBarViewModel` on the overlay's own back-stack entry, and the
page-attachment family — `AskPagesRepository`, `AttachedPage`, `TaskScope`,
`AttachPagesSheet` and the page chips — moved here with the composer that
draws it, since the chips are the consent for what Taffy reads and the start
rule reads them. The conversation inside the card is the assistant feature's
`AskConversationPanel`, reaching the overlay as a slot the shell fills,
because a feature depends on no other feature (decision 0135).

This feature draws browser chrome and projects immutable state. It renders no
web content and depends on no other feature. Tabs, navigation, page
observations, and task progress arrive through declared ports.

`BrowsingViewModels` contributes each assisted ViewModel creator to Dagger's
multibound map. `DaggerViewModelFactory` resolves that map; there is no central
class-literal switch, and no second constructor table beside it.

The address bar always displays its chosen reading before committing it. A task
reading becomes a Core API intent; authorization remains exclusively in the
policy path. The feature never creates an authorization fact, capability, task
runtime, workspace store, or browser implementation.

## Three things this feature will not draw

Each was proposed, has a mock, and is deliberately absent. The catalog rows for
SCR-101, SCR-102 and SCR-104
carry the same statements; this is the list a reader of the code needs.

**Nothing covers a loading page.** A page that is loading keeps drawing, and
the only thing that says so is a 2 dp indeterminate rail on the address pill's
lower edge — `textPrimary`, not the accent, because amber means Taffy and a
page load is not Taffy. It is indeterminate because `NavigationState` carries
`isLoading` and no fraction, so anything that filled would be inventing
progress. Reduced motion draws it full width and still, which is present and
unambiguous without moving. `BrowserContent` has three cases — `PAGE`,
`FAILED`, `START` — and no loading case at all, which is what makes the rule
structural: there is no state in which an opaque skeleton can come back without
someone adding one. The address bar and action row overlay the page so hiding
the row does not resize the engine surface; that overlay is chrome, not a cover
of a page that is still loading.
ux-spec.md section 2 is the authority.

**No wallpaper on the start page.** SCR-102's mock has one. There is no asset,
no store, no setting and no catalog row behind it, so building it would mean
inventing a feature and shipping a stock image with it. The full-bleed ground
`StartPageBackdrop` paints (decision
0050)
is derived entirely from theme tokens for exactly this reason, and nothing on
the start page is fetched, suggested, or ranked by anyone but the person: the
tile grid is the profile's own visit counts, kept on this device, and it is one
row of four, because a second row pushes it past the fold it sits above. A
private tab's start page draws no grid at all, because those counts are the
regular profile's (decision
0255).

The painted plate above the wordmark is not that wallpaper and does not
weaken this. `TaffyStartSceneImage` is a bounded first-party illustration
inside the content column — the slot one compiled-in scene already
occupied — picked by this device's own clock and by nothing else. It arrives
through the delivery plane rather than being fetched when the page opens, so
the surface still reaches no network; decision
0145
squares the two and is the place that argument is kept.

**No remote tab artwork.** SCR-104's grid draws favicons and page snapshots from
the local engine cache, the start page's weather and the brand mark on a tab
that has been nowhere — no duration chip, because that tab has no visit to
time — and a silent slab when no snapshot has been captured. The start page's frequent-site
tiles follow the same rule with one more local source: an open tab's engine
favicon first, then the profile's own favicon database — the mark the engine
saved when the person visited the site, which is what keeps a tile pictured
after its tab closes and across restarts — and the site's own initial drawn
locally on a sweep of the 3D mark's hues only when neither store has ever seen it.
A letter is honest on a tile that names its host underneath, where SCR-104's
cards would be picturing nothing. The mock's `cdn.simpleicons.org` images are chrome reaching the
network, which browser chrome must never do.

## The chrome controls are shared, on purpose

`BrowserChromeControls.kt` owns `BrowserChromeButton` and `BrowserTabsButton`,
and both action rows use them. The tabs button takes two counts rather than a
`BrowserMainUiState`, which is the whole reason the start row can call it: a
start page has no browsing state, and a second copy of the badge geometry is
how two action rows drift apart a pixel at a time.

It owns `BrowserStartActionRow` as well, and that is the same rule one level
up. **Which row a screen draws is decided by what the tab is showing, not by
which screen it is.** SCR-101 drew the page row over its own start content, so
a tab that had been nowhere carried a Back and a Forward that could never do
anything and a Share over a page that was not there, while the identical
moment reached through SCR-102 was given the start page's slots. A page gets
`BrowserActionRow`; a tab that has been nowhere gets `BrowserStartActionRow`,
on either screen. The start row is one centred dock of four plain
destinations — Downloads, Workspaces, Tabs, Settings — so the gear goes
straight to Settings on both hosts.

That rule now has a second input, and only one: **a tab that has been nowhere
gets `BrowserStartAssistantRow` instead while the assistant bar is speaking for
a task**, which is the pill with Tabs beside it (decision 0140, amending
decision 0132 section 2). The fact comes from `TakeoverUiState.hasTask`, which
this feature already computes — `projectTakeover` reads the task repository
this module already depends on, so nothing here depends on the assistant
feature and no edge is added to the component graph. It is deliberately wider
than `TakeoverUiState.active`: `active` is the task's own `isUnderWay` and must
go false the moment work stops, so that the frame comes off the page, and a
task that has finished still has one line left to say.

It is read beside `TakeoverUiState.taskEnded`, and the pair gives the row three
shapes rather than two (decision 0141). A task that has **not** ended closes
back, forward and tabs: all three are refused while Taffy is driving —
`TakeoverInputLock` holds the page against a second driver — so drawing them
would put three dead targets in the three positions nearest the thumb, next to
the one thing in the row with words in it. They close in place through
`BrowserClosingSlot` rather than the row being swapped for another, so the pill
stretches into what they leave as one movement, and all three open again the
moment the task ends. The **takeover band is gone**
with the same decision: it stood above this row carrying a mode chip, a second
status line and Take over, and the pill one row below now carries all three.
Over a page, the action row's far-edge
control is a three-dot button opening `BrowserMenu`: an anchored grid of
grouped tiles (the site's own actions when there is a page, the browser's
destinations always), which replaced `BrowserMoreSheet` — the options now
appear over the control that asked for them rather than at the far edge of
the screen under a scrim.

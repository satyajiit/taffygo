# `:core:ui`

**Status:** `[Current]` The shared components, the navigation contract, and the
string seam.

Owning milestone: M0, work package
WP-M0-07.

Authoritative specifications:
android-app-architecture.md
sections 5 and 7;
screen-catalog.md for the screen
identifiers destinations carry;
browser-parity-matrix.md
rows PAR-A11Y-002, PAR-A11Y-003, PAR-A11Y-004 and PAR-L10N-001.

## The authority boundary

**This module owns how a Taffy surface is built and how one screen reaches
another.** Features depend on it; it depends on no feature, and the module-graph
check refuses the reverse edge. Two things live here because more than one
feature needs them and two copies would drift:

- **the navigation contract.** `TaffyDestination` is the closed set of places a
  screen can be, each carrying its catalog identifier and its route. `BackStack`
  is a pure reducer over that set, so back behaviour is a unit test rather than a
  device test. `DestinationViewModelStoreOwner` gives each destination its own
  store, its own arguments, and its own saved-state namespace while keeping the
  activity's injected factory. Cross-feature navigation goes through this
  contract and never through a direct reference to another feature;
- **the accessibility primitives.** A status is a word, a shape, and a tone —
  all three, always, because `StatusPresentation` cannot be constructed without
  them. The control bar shows the same three controls, in the same order, at the
  same size, ahead of the detail below them. A list row is one accessible element
  with one description.

The string seam is here too. `taffyString` and `taffyPlural` read the resource
and pass it through the transformer in scope, which is how the pseudo-localized
variant reaches every surface without a screen knowing about it. `taffyCount`
renders a bare count with the composition's locale, so a badge digit follows
the chosen language the way a word does.

## What this module deliberately does not do

It contains no screen, no view model, and no repository. It knows no feature by
name. It holds no product copy beyond the words shared by more than one feature —
the seven task-state words, the download states, the mode chips, the three
controls, and the accessible pair separator. Everything else belongs to the
feature that shows it, and the string check fails a module that reaches for
another module's names.

Nothing here decides where the system bars are. `TaffyScreen` applies the four
edges named in `:core:designsystem`, and it applies each one where it belongs —
the sides to the frame, the top to the title bar, the bottom inside the scroll —
so a screen passes under the bars while its words and controls stay clear of
them. A surface that took a number instead of a padding modifier would break
that arithmetic for everything below it.

It does not depend on a navigation library: the catalog's frozen dependency set
has no Compose navigation entry, and the contract above is the deliberate
replacement rather than a wrapper around a missing one.

## Two icon objects, on purpose

`TaffyIcon` and `ProviderMark` hold the same kind of bytes and are held to
different standards, so they are two objects in two files with two provenance
records. A Phosphor glyph is licensed — MIT, one licence text, a count that has
to match. A provider mark is a trademark, permitted for one use and licensed
not at all, and decision
0030 makes each
one carry an owner, an upstream URL, a fetch date, an upstream checksum, a
conversion command, a committed checksum, a terms URL, its permitted use, and
the trigger that removes it.

The split has a mechanical consequence worth stating: the Phosphor gate counts
`val <Name>: ImageVector` declarations in `TaffyIcon.kt`, so a mark added to
that file would silently move the glyph count. Keeping marks in their own
object means adding one can never be mistaken for adding a glyph.

A mark that cannot produce both an upstream URL and an upstream checksum is not
vendored at all. Three of the launch providers cannot, and their rows draw a
typographic monogram from the vendor's own displayed name instead —
`vendor/provider-marks.txt` names which three and why.

## What each area owns

| Area | Owns |
|---|---|
| `TaffyDestination` | The closed set of destinations and their catalog identifiers |
| `BackStack`, `TaffyNavigator` | Where back goes, as a pure reducer and a seam, including the whole-stack replacement that leaves the first-run sequence behind |
| `ScreenViewModel` | Per-destination view-model stores, arguments, and saved state |
| `StatusPresentation` | Word, shape, and tone for every status the UI host shows |
| `TaffyScreen`, `TaffyTopBar` | The frame every screen sits in, a Back row (chevron and the word) above the title when `onBack` is set, and an optional sticky footer that consumes the bottom inset once |
| `TaffyCategoryTile`, `TaffyStatTile`, `TaffySwitch`, `TaffyHeroCard` | Shared Soft Pulse tiles (chapter host 76 / min 106), the one switch, and the destination-canvas hero (2:3 bleed) |
| `TaffyBentoColumns`, `TaffyBentoGrid`, `TaffyBentoTile`, `TaffyBentoSpan` | The bento grid of decision 0103: the one fold rule (2 / 3 / 4 columns from the width the grid was given, one column at large text or under 340 dp), the eager grid that packs spanned tiles row by row, the destination tile with a glyph well in its wash's solid, and the span a tile claims |
| `TaffyLazyGridScreen`, `taffyBentoItem`, `taffyBentoItems` | The trusted screen frame over a lazy grid, and the item helpers that clamp a span to the line; the container test tag goes on the grid through `gridModifier` |
| `TaffyTileWash` | The wash behind a glyph and, on a bento tile, the ground of the tile; the three ribbon cases are hub-group identity, and `inkColor` answers the ordinary ink for them because a ribbon hue never carries text |
| `TaffyDestinationCanvas` | Named index of the destination-canvas recipe (hero, section bar, object card, info tile, stacked stats) |
| `TaffyObjectCard`, `TaffyInfoTile`, `TaffySectionBar`, `TaffyTwoUp` | Destination-canvas recipe: hairline card, receding help tile, hue-bar section, stacked full-width pair (never a side-by-side grid). `TaffySectionBar` takes an optional `count` beside its title with its own `countDescription`, so a list's size is not formatted into a per-language title string |
| `TaffyPageSurface`, `LocalPageSurface`, `PageSurfaceSlot`, `LocalPageSurfaceSlot` | The web page as a platform view, and where it is. The surface belongs to the window, so it is composed once by the navigation host and never unmounted: a screen carrying the page reports its rectangle through the slot and the host places it, and a destination that does not carry it parks it off the bottom rather than hiding it. `LocalPageSurfaceSlot` defaults to null, so previews and semantics tests compose the page in place exactly as before |
| `TaffyBottomSheet` | The reusable tonal modal-sheet frame, handle, title, keyboard/navigation inset handling, and dismissal contract; it has no full-surface rim |
| `TaffyProjectContact`, `openEmailDraft`, `installedVersionName` | The two places a person can reach the people who make TaffyGo, each written once: the report mailbox and the public repository, with the `mailto:` draft address and the new-issue address built from them (decision 0253). `openEmailDraft` hands a draft to the person's own email app with `ACTION_SENDTO` and answers false when no app takes it; nothing is sent from TaffyGo, which has no server to send to (decision 0200). About's source row, Help's feedback routes and the report sheet under an answer all read these |
| `TaffyTwoPane`, `TaffyPaneSplit` | Compact single pane, or list-detail / content-and-Taffy on a wider window |
| `TaffyDestinationGroups` | Which destinations share a list-detail pair |
| `TaffyControlBar` | Pause, stop, and take over, always in that order |
| `TaffyListRow`, `TaffyKeyValueRow` | One row, one announcement |
| `TaffyEmptyState` | Empty as a state that is rendered, not a blank area; an illustration slot replaces the glyph well when both would draw |
| `TaffySetupNeededPanel` | The one panel an AI surface draws when Taffy has no provider to reach: `TaffyEmptyState` with the mark, a full-width action that opens AI & providers and an optional way out; the caller owns every sentence, because which one is true depends on what it knows |
| `TaffyBrandMark` | The mark at five densities, in the treatment the theme in scope asks for; the words beside it belong to the caller |
| `TaffySavedFlowSceneImage` | The original local illustration a saved flow is shown beside, separate from page previews and result state. The parent owns the modest size and omits the artwork when larger text needs the space; the image adds no click action or announcement. Masters, generation prompts and checked shipping bytes live under `vendor/task-scenes/` and the adjacent provenance records |
| `TaffyStartSceneImage`, `StartSceneSource`, `TaffyStartScene` | The start page's plate, chosen by the device's own clock: four parts of the day and six paintings in each, picked by the date so it is the same all day and different tomorrow. The paintings arrive through the delivery plane as the `start-scenes` pack and are read off this disk a member at a time, so the surface opens no socket (decision 0145); the browsing plate compiled into the installer stands in until the pack lands. `LocalStartSceneSource` defaults to that fallback, so a preview and an unbound host draw a picture and ask for nothing |
| `TaffyMarqueeText` | One line that travels out and back when it does not fit, so the whole of it can be read. The assistant pill's line is why it exists: it is the product's one statement of what Taffy is doing and the row leaves it less width than the sentences the core sends. Reduced motion keeps the ellipsis |
| `TaffyIcon` | The vendored Phosphor glyphs every surface draws from, as path data rather than a dependency; `vendor/phosphor.txt` names and counts them, and the vendored-asset gate holds the two together |
| `ProviderMark` | The vendored provider brand marks, apart from `TaffyIcon` for the reason below; `vendor/provider-marks.txt` records their provenance and the gate checksums the file |
| `taffyString`, `taffyPlural`, `taffyCount` | The string seam, the transformer in scope, and the locale-aware count |
| `src/debug/.../*Previews.kt` | Android Studio annotations, fixed states, and preview functions; debug variants only, never a GN or release input |

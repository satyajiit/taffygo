# `:feature:assistant`

**Status:** `[Current]` The assistant's surfaces: SCR-301 and SCR-303, the
panel that shows Taffy working under the start page's box, and the
page-intelligence inspector.

Owning milestone: M3 (the assistant and workspaces) for the task surfaces, and
milestone M2 (page intelligence) for the inspector. Built here under work package
WP-M0-07.

Authoritative specifications:
screen-catalog.md rows SCR-301
and SCR-303;
ux-spec.md sections 4 and 6;
domain-model.md sections 9
to 12;
browser-intelligence-protocol.md
sections 6 and 7.

## The authority boundary

**Every task state this feature shows comes from the generated Core API.** The task
surface sends typed intent through `:core:task` and renders the immutable
browser/Rust projection that comes back. It computes no canonical task state of
its own. The bar lines are a total function over the closed display state, so a
new state cannot arrive with an empty line. Page cards receive the browser's
tab projection and local artwork through `BrowserRepository`; Compose cannot
assign a tab to a task.

Two sentences the product depends on are asserted here rather than assumed:

- **partial work keeps its own words.** The partly-done line says what was read
  and what was not, and no path rounds it up to success;
- **stopped is what the user did.** It is never conflated with a failure, and it
  has its own word, its own shape, and its own line.

The inspector shows a browser-projected `PageSnapshot` and nothing else. A
malformed or unsupported observation is refused before it reaches the screen.
A withheld value arrives as the withheld marker and there is no field on the
row type that could carry the content instead. Golden BIP documents exercise
the same projection under test and never enter the product closure.

## What this feature deliberately does not do

Task progress and controls use local templates with typed values filled in — a
count, a host, a state word. Page titles and the task's visible answer remain
content in their named panels. Neither can define a control or change what
the browser permits.

It takes no credential. When a site needs a sign-in the surface offers taking
over the tab; there is no password field anywhere in this module and there will
not be one.

It does not run the task. Commands go through the browser Core API to the
sandboxed Rust service, and only its immutable projection returns to this
feature.

## What each screen owns

| Screen | Catalog row | Owns |
|---|---|---|
| `AssistantBar` | SCR-301 | The Assistant pill hosted in the browser action row on a phone. One 48 dp pill in every one of its nine states and never a second surface, because the action row it sits in is a fixed height and clips what it is handed. Its idle state opens task setup; the others show one truthful status line, take over where the revision admits it, and the one control that state needs — pause while running, review while waiting, resume where the revision admits it — while stop is one tap away on the task view (decisions 0140 and 0141). The takeover band that used to stand above the row is gone, so this pill is the whole of the bottom chrome's claim about a task: the mode leads what it announces rather than being drawn beside it, and a line too long for the row travels out and back rather than truncating. A finished task's pill is itself the way to the results; when nothing is set up its idle line says so, and its tap opens the Ask overlay (the browsing feature's frame and composer), which holds the set-up panel |
| `AskConversationPanel` | SCR-301 slot | The conversation inside the Ask overlay's card, reaching the overlay as a slot the shell fills: each exchange's question with Taffy's answer streaming under it, and beneath the exchanges the start page's task panel without its title, its second door reading Close. The answer block it draws is the one SCR-303's answer panel draws |
| `TaskViewScreen` | SCR-303 | The house top bar and controls, the exact task's browser tabs in paired cards, one Taffy activity panel below them, then timeline, sources and output. Cards use local engine artwork and open the actual tab, retaining the workspace on Back. On a tablet the pages and timeline sit beside the sources. Under a failed task's state word, the reason, and the set-up offer when the reason is a provider Taffy could not reach. The screen takes `TaffyScreen`'s own bar rather than painting one: Back is the shared chip with the word in it, the title is the task's goal with heading semantics, "Your request" is the subtitle, and the status chip is the bar's one trailing control. The plate that used to repeat the goal above the timeline is gone with the row it belonged to — its whole content is the bar's title and subtitle now |
| `ReportAnswerSheet` | SCR-301, SCR-303 | Report this response, drawn by the shared answer block under every answer that has finished arriving, so both surfaces that show one offer it. The sheet offers an email draft first — the person's own email app opens it, holding the answer cut at 4,000 characters, the providers the readiness rule says could answer (`ProviderReadinessFacts.answeringProviderIds`) with the model chosen for each, and the app version — and a public GitHub issue second, opened in a tab with a title and none of the answer, because an issue is public. Nothing is sent from TaffyGo, which has no server (decision 0253). The core does not say which provider wrote a given answer, so the draft names every one that could have rather than guess |
| `StartPageTaskPanel` | SCR-102 slot | Taffy working, under the start page's box: the goal, the moving line, the rail and the controls, until the task is on a page |
| `PageInspectorScreen` | — | A decoded snapshot: graph, provenance, sensitivity, truncation |

## Where a task starts

`TaskBrowserViewModel` reads tab ownership from the browser alongside the
followed task. A page belongs in that task's cards only when its `taskId`
matches exactly; an assistant-created flag, a matching host or a page title
cannot establish that association. Private pages are excluded. The same check
runs again when a person opens a card, and the browser must confirm its live
tab and exact task before navigation occurs. Artwork is read from the browser's
existing local cache, with no screenshot request or network access from the
workspace. The single activity panel explains running, paused, waiting and
terminal states without inventing per-page progress.

`TaskSkillReviewViewModel` offers a recorded flow only when the core associates
it with the exact completed task. Review shows the full public starting address
and every ordered action, including handovers. Save names the reviewed immutable
version; only a later active publication confirms that it was saved. Legacy or
incomplete review data produces no task offer. The same review component serves
Skills settings, where pending recordings remain available after Not now.

There is no consent screen before a task and no separate picker for the shape
of the answer. A task starts where it was typed: the Ask overlay (SCR-301), whose
composer is the start page's own box on a second host, starts every ask
itself and shows the consent under its field, with the set-up panel in place
of the field while Taffy has no provider to reach; the start page's box starts
a task in place and shows the same consent under the reading; the tab
switcher's Ask Taffy opens the overlay with the picked tabs attached, and New
workspace opens it with the source-table shape stated. The one start rule
lives in `:core:task` and every composer calls it. Once a task has answered,
the overlay's box takes the next question as a follow-up on the same
conversation, and this feature's `AskConversationPanel` shows the exchanges
(decision 0137).

## Verification list

Ordered, and each item is a gap rather than a silent hole:

1. **The inspector renders page snapshots only.** Seven of the seventeen golden
   documents are page snapshots; the rest are deltas, actions, and protocol
   information. The surface says how many it does not render rather than
   pretending the contract has fewer documents than it has.
2. **"See results" opens the workspaces; it does not fabricate one.** A
   workspace appears only when the generated Core API projects Rust-owned
   workspace state. Producing source and fact material from a finished task is
   core-service work and never belongs to this bar.
3. **Readiness is a fact, never the preference.** `TaffyReadinessRepository`
   in `:core:task` reads the provider roster, the account's plan and the
   credential handles this phone holds; the saved route only chooses which
   sentence the set-up panel says. A surface that read the preference to
   decide whether Taffy can answer would say "not set up" while SCR-404
   lists a connected provider, or the reverse.

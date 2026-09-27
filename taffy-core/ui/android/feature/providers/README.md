# `:feature:providers`

**Status:** `[Current]` Every provider surface: screen SCR-404, the hub a
person actually uses, and the five destinations it leads to — SCR-415, SCR-416,
SCR-417, SCR-418 and SCR-419. SCR-404, SCR-415 and SCR-416 are built. The rest
are reachable destinations that draw their own title and nothing else.

Owning milestone: M3, under work package
WP-M0-07.

Authoritative specifications:
screen-catalog.md row SCR-404,
which these screens divide;
android-app-architecture.md
section 5 for the navigation contract; decision
0080
for where a provider roster comes from, and decision
0081
for which vendor sign-ins a binary carries.

## The authority boundary

**This module owns provider surfaces, not provider facts.** The merged catalog
is published by the isolated core and arrives as `ProviderRosterRow` through
`:core:providers`; credentials are held by the browser and reached through
`:core:credentials`. A screen here proposes and displays. It mints no
credential, holds no key material, and decides no catalog membership.

**SCR-404 selects and never edits.** The hub navigates and writes nothing at
all, so it has no draft, no in-flight flag and no local opinion about what a
person just did. Saving a key, signing in to a plan and describing an endpoint
each belong to the surface that owns that credential, which is also why the hub
cannot open a credential form for a provider that takes no credential.

**The four categories are tabs, and the tab is the only thing this screen
remembers.** They are alternatives rather than parts of one list — Connected,
Subscription, API keys, Your own — so stacking them as four sections made every
one of them something to scroll past on the way to the others. The chosen tab
lives in `ProviderHubUiState`, changes through `ProviderHubIntent.ShowCategory`,
and is folded back on by `ProviderHubReducer` after each fresh projection, so a
roster republication cannot move somebody off the tab they are reading. A tab's
count is its section's row list measured, never a stored number, and a tab that
holds nothing is still drawn and says in words why it is empty. The endpoint tab
is called **Your own** rather than *Local*: there is no on-device model runtime
and none is selected (decision
0060),
so what belongs under it is a server the person runs somewhere this browser can
reach — never one running on the phone.

**SCR-415 is where a provider is changed, and it keeps to one road at a time.**
Where a provider offers both a plan and a key, the sign-in is a block of its own
above a rule reading "or paste a key" — never two primary buttons side by side,
which is how somebody pastes a key into a provider they could have signed in to
in one tap. A key is proved with one bounded call before it is stored (decision
0083),
a probe verdict never reaches a person raw, and the catalog's served key prefix
refuses a badly-shaped draft before a call is spent on it.

**A default is only claimed where the product could honour it.** Nothing in the
roster names a default provider; the one fact the product holds is the route.
So `ProviderDefaultChoice` has four members, and two of them draw a sentence
instead of a control: nothing is connected, or several things are and the
product cannot say which of them a request goes to. The rule itself is
`ProviderRowDispatch.standingChoice`, shared with the hub's badge so the two
screens cannot disagree in front of somebody reading both.

**No screen decides anything from a provider's identity.** The catalog is
served, so the set of providers changes without a release, and a rule that
named `"anthropic"` would be correct only beside the catalog it was compiled
with. `ProviderRowDispatch` reads facts — the methods on offer, whether this
build can act on the row, whether a credential is stored, whether the catalog
holds it shut, whether the address moved, which layer supplied it — and nothing
else. The one identity lookup left is the brand mark, which is a picture of who
a provider is and never a claim about what they offer.

**A row that this build cannot act on is explained, never offered.** A provider
offering only a sign-in flow this binary does not compile renders as
unavailable with the reason and no click action. Degrading it into a key form
would be a control that cannot work, which is worse than an absent one.

## What this feature deliberately does not do

It knows no other feature. A screen carries a destination from the navigation
contract in `:core:ui`, and the shell resolves it.

It owns no storage. Ordinary preferences belong to `:core:preferences` and
provider material to `:core:credentials`; this module reads both and writes
neither.

## What each screen owns

| Screen | Catalog row | Owns |
|---|---|---|
| `ProviderHubScreen` | SCR-404 | Every provider this browser can reach a model through, under four tabs with counts, each row saying where it stands and where pressing it goes |
| `ProviderConfigScreen` | SCR-415 | One provider's page: its credential, its pinned model, and how much thinking it is asked for |
| `ProviderSignInScreen` | SCR-416 | One vendor's sign-in, for the vendors this binary compiles a flow for |
| `ModelSelectionScreen` | SCR-417 | The models on offer, for one provider or across the whole catalog |
| `CustomEndpointSetupScreen` | SCR-418 | Setting up a provider whose address the person supplies, including the probe of that address |
| `ConnectedProvidersScreen` | SCR-419 | Every provider this browser can currently reach a model through |

## How one row is decided

`ProviderRowDispatch.offerFor` answers in this order, and the order is the
substance:

1. the roster says this build cannot act on the row — listed and explained;
2. the address moved and the core refused it — listed and explained;
3. the catalog holds the provider shut — listed and explained;
4. the person supplied the provider — SCR-418, where they typed it;
5. a credential is stored — SCR-415, because something stored is something to
   manage, whatever method saved it;
6. a key is on offer — SCR-415;
7. only a plan is on offer — SCR-416 when this binary compiles that vendor's
   flow, and listed and explained when it does not.

Which tab a row appears under follows the same facts: a credential files a row
under **Connected** whatever way it arrived, then a person's own address, then
a plan-only row, then everything else. Connected is a tab rather than a badge
scattered through the other three because a person who already set this up
comes back to one question — what am I using, and is it working — and the
answer to it must not be four presses that each show a third of it.

## Verification list

Ordered, and each item is a gap rather than a silent hole:

1. **SCR-404, SCR-415 and SCR-416 are built; the rest are not.** Each of the
   others draws its title and says nothing it cannot prove. What they will own
   arrives with the state that can prove it.
2. **Nothing on the hub writes; SCR-415 does.** The hub calls no repository
   command at all. Saving a key, removing a credential and moving the route are
   `ProviderConfigViewModel`'s, and each one is a published fact coming back
   rather than a local claim — the page cannot say connected about something the
   core has not filed.
3. **SCR-416 cancels one exact flow.** Portable removal is accepted before the
   browser stops native work, and the screen says cancelled only after that
   verdict. The countdown beside the code remains this screen's own waiting
   window because the vendor's expiry never reaches Android.
4. **The compiled flow map is checked against the core's.**
   `ProviderSignInFlowsParityTest` in `:core:providerauth` reads the core's
   `SIGN_IN_VENDORS` and the compiled catalog off disk. The browser's own pinned
   table is C++ and out of reach; it remains a third hand-kept copy nothing here
   verifies.
5. **Connected and Default are two badges.** `ProviderHubProjectionTest` holds
   two credentials and asserts that Connected is true twice and Default never,
   because nothing in the product names which of two keys is in force.
6. **A transient failure never re-locks a working row.** A refresh that failed,
   a sign-in the vendor wants again, and a browser handle the roster has not
   echoed all read as unknown rather than absent.
7. **Every destination round-trips.** `TaffyDestinationTest` restores all five
   from their routes, including both shapes of SCR-417 and SCR-418, which is
   what stops process death from crashing on the way back.

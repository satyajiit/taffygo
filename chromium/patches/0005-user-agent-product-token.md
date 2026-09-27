# 0005 — Name TaffyGo in the user agent brand list

**Status:** **Reinstated** 2026-09-07, on a measurement. The 2026-08-21
retirement below was made without ever reading a header off a device, and one
of its three reasons does not survive one being read. This specification is
rewritten to the change the measurement argues for; the retirement text is kept
underneath it, because it is the argument this reverses and two of its three
reasons still stand.

**Needed by:** WP-M1-01 — the product identity seam in
`//taffy/common/public/taffy_product_identity.h`
**Estimated size:** ~12 modified upstream lines, 2 files

## What the device actually sends

Measured 2026-09-07 on the Xiaomi phone against an HTTP echo served from the
build host over `adb reverse`, so nothing left the machine. TaffyGo was pinned
with `am start -p com.taffygo.browser`; the reported
`Sec-CH-UA-Full-Version-List` reads `152.0.7977.42`, which is this queue's
pinned tag and is how the reading is known to be TaffyGo rather than the
system's default browser:

```
User-Agent: Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Mobile Safari/537.36
sec-ch-ua: "Not?A_Brand";v="24", "Chromium";v="152"
sec-ch-ua-full-version-list: "Not?A_Brand";v="24.0.0.0", "Chromium";v="152.0.7977.42"
navigator.userAgentData.brands = [ Not?A_Brand 24, Chromium 152 ]
```

Two facts follow, and they point in opposite directions.

**The user agent string carries no product identity to keep or lose.**
`ReduceUserAgentMinorVersion` is `stable` in Blink at this pin, so the platform
is the frozen literal `Linux; Android 10; K` and the version is frozen to
`152.0.0.0`. Every Chromium derivative on every Android device sends that same
string. It is not TaffyGo's identity being replaced by Chromium's; it is a
string that has been deliberately emptied of identity by the platform.

**The brand list is where identity was moved to, and TaffyGo is absent from
it.** That absence is what a person sees: an account activity page with no
brand to name falls back to the platform, which is why signing in reported the
device as "Android". A brand list is the surface the UA-CH specification exists
to provide, and it is the one surface a derivative is *expected* to add itself
to.

## The correction to the retired specification

The retired text named the wrong seam, and had it been written as specified it
would have produced the exact fingerprinting inconsistency its own point 3
refuses.

`ChromeContentBrowserClient::GetProduct()` and `::GetUserAgent()` build the
user agent **string** and nothing else. Neither one reaches `Sec-CH-UA` or
`navigator.userAgentData`: those come from `blink::UserAgentMetadata`, built by
`embedder_support::GetUserAgentMetadata()`, which is a separate call chain.
A token supplied through the two functions the retired specification named
would have appeared in the string and in neither brand surface.

## Upstream file and symbol

| | |
|---|---|
| File | `//chrome/browser/chrome_content_browser_client.cc` |
| Symbol | `ChromeContentBrowserClient::GetUserAgentMetadata()` |
| File | `//chrome/browser/client_hints/client_hints_factory.cc` |
| Symbol | `ClientHintsFactory::BuildServiceInstanceForBrowserContext()` |
| File (hook 2, not recommended) | `//chrome/browser/chrome_content_browser_client.cc` |
| Symbol | `ChromeContentBrowserClient::GetUserAgent()` and `::GetProduct()` |

Both files are `//chrome`, which is the layer `//taffy` is reachable from:
`chrome/browser/DEPS` carries `+taffy/browser`. That is not incidental — it is
what rules out the one-file version of this change, below.

## The change

Two hooks, and they are independently landable. The first is the one this
specification recommends; the second is recorded because the scope asked for
it, and is argued against below.

### Hook 1 — the brand list. Recommended.

```cpp
blink::UserAgentMetadata ChromeContentBrowserClient::GetUserAgentMetadata() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  blink::UserAgentMetadata metadata = embedder_support::GetUserAgentMetadata();
  taffy::AddProductBrand(metadata);
  return metadata;
}
```

`taffy::AddProductBrand` is defined in `//taffy/browser` beside
`GetProductIdentity()`, and no brand string is written in the upstream file. It
appends `{product_name, major version}` to `brand_version_list`, and
`{product_name, full version}` to `brand_full_version_list` — but only when
that second list is non-empty, because
`GetUserAgentMetadata(only_low_entropy_ch=true)` leaves it empty on purpose and
filling it would hand a high-entropy hint to a caller that asked not to have
one. It appends nothing when `is_taffy_branded` is false or the name is empty,
so the clean-host upstream baseline is untouched.

The brand version is Chromium's version, not a TaffyGo one. That is what
`GenerateBrandVersionList` already does for a browser brand, and it keeps this
change from inventing a second product-version owner —
`//taffy/common/public/taffy_product_identity.h` deliberately holds no version
for the same reason.

The entry is appended after upstream's `ShuffleBrandList`, so TaffyGo is always
last rather than shuffled among the others. That is not a fingerprinting
concern: the brand name is the identity, so its position discloses nothing the
name has not already disclosed.

**It takes two hooks, not one, and the reason is the whole of why this
specification is worth reading twice.** The metadata a page sees and the
metadata a header carries come from two different interfaces:

| Surface | Reached through |
|---|---|
| `navigator.userAgentData.brands` | `render_process_host_impl.cc` → `ContentBrowserClient::GetUserAgentMetadata()` |
| Service and shared workers | `service_worker_version.cc`, `shared_worker_host.cc` → the same |
| `Sec-CH-UA`, `Sec-CH-UA-Full-Version-List` | `client_hints.cc` → **`ClientHintsControllerDelegate::GetUserAgentMetadata()`** |

`client_hints::ClientHints::GetUserAgentMetadata()` calls
`embedder_support::GetUserAgentMetadata()` directly and never reaches the
content browser client at all. So hooking the browser client alone brands
`navigator.userAgentData` and leaves `Sec-CH-UA` unbranded — which is exactly
the fingerprinting inconsistency the retirement's reason 3 refuses, arrived at
from the other direction. An earlier draft of this specification claimed one
hook covered all three; it does not, and the claim is recorded here rather than
quietly fixed because it is the kind of error that reads as correct.

The second hook is therefore the delegate's construction.
`client_hints::ClientHints` is neither final nor sealed and its constructor is
public, so `//taffy` subclasses it and overrides one method to call
`AddProductBrand`; `ClientHintsFactory::BuildServiceInstanceForBrowserContext`
constructs the subclass instead. That keeps both hooks in `//chrome`, where
`+taffy/browser` is already allowed.

### Hook 2 — the user agent string. Recorded, and argued against.

```cpp
std::string ChromeContentBrowserClient::GetUserAgent() {
  const std::string& token = taffy::GetProductIdentity().user_agent_product_token;
  if (token.empty()) {
    return embedder_support::GetUserAgent();
  }
  return <the upstream string with the token appended>;
}
```

Reason 2 of the retirement is unchanged by the measurement and still refuses
this: the string is compatibility-visible, sites sniff it, and the parity
matrix's PAR-WEB rows promise upstream rendering behaviour. The measurement
adds a second reason the retirement could not have known. The string is frozen
by `ReduceUserAgentMinorVersion` precisely so that it carries no per-browser
entropy; appending a token puts entropy back into the one field the platform
spent years taking it out of, and does so for every request, to every site,
forever. The brand list already carries the name to any site that asks.

`user_agent_product_token` therefore stays empty, and this half stays
unexported unless the owner decides otherwise. Decision
0130
is where that choice is recorded.

## Why the overlay cannot host it

There is no first-party seam. Three were checked at the pin and all three are
closed:

1. `embedder_support::GetUserAgentMetadata()` takes no additional brand. The
   underlying `GetUserAgentBrandMajorVersionList` and
   `GetUserAgentBrandFullVersionList` both accept an
   `additional_brand_version`, and it is exactly the parameter this change
   wants — but `GetUserAgentMetadata` passes `std::nullopt` to both as a
   literal, and nothing in `//chrome` passes a value.

   This function is also the one place the two call chains above converge, so a
   single hook here would cover every surface. It is still refused, and the
   reason is a rule rather than a preference:
   `components/embedder_support/DEPS` does not admit `+taffy` and must not, a
   shared component may not depend on one product, and the alternative — adding
   a settable brand to `embedder_support`'s public API for one embedder to
   populate at startup — is a larger upstream delta than the two thin hooks
   this specification asks for. Two hooks in the right layer beat one hook in
   the wrong one.
2. The `branding_file_path` GN argument is declared and settable, and it does
   change `version_info::GetProductName()`. It does not help:
   `GetUserAgentBrandList` reads that name only under
   `#if !BUILDFLAG(CHROMIUM_BRANDING)`, and that buildflag is derived from
   `is_chrome_branded` alone, which this build cannot set.
3. The GREASE brand is generated from a seed with no feature parameter to
   override it, and it would be the wrong slot regardless: GREASE exists to be
   ignored.

`content::WebContents::SetUserAgentOverride` can carry a metadata override
per tab, and is rejected on its merits rather than for layering. It would have
to be applied at every tab-creation site, with a missed site silently
unbranded; it does not reach a request that has no `WebContents`; and it writes
the one override slot that `TabUtils.switchUserAgent` already owns for the
desktop-site toggle, which `ChromiumSiteInfoRepository` ships today.

## Rebase risk

**Low for the code, and the risk that matters is not the code.** Both hooks
wrap one call each and add no upstream logic. The failure mode is the one this
specification has already hit once at review: a third call chain to the same
metadata, or an existing one moving, leaves both patches applying cleanly while
covering less than they claim — and a partially branded browser is worse than
an unbranded one, because it is a fingerprinting signal rather than a name.
Nothing static can see it. The check is the device measurement above, which
reads all three surfaces in one navigation and is cheap to repeat at every
rebase.

## The fork-debt bound blocks the export

`./tools/check fast --only chromium` on 2026-09-07 reads 45 patches against a
budget of 40 and 2,420 modified upstream lines against a budget of 1,500 —
113% and 161%. `chromium/patches/README.md` states that exceeding either bound
"blocks new upstream-file edits until the delta is refactored into `//taffy` or
upstreamed", so no commit on `taffy/patched` and no `export-patches` run may
carry this change yet. Un-retiring the specification does not violate that: a
specification is a document, the queue's own rule is that a change is
"specified first", and an unexported specification is counted as projected
debt rather than realised. This one is projected at ~12 lines against a
realised figure that is already over.

Shedding is the precondition, not this change. The queue must be back inside
both bounds first.

## Verify at SP-01 — answered

1. **Whether the token can be supplied through an existing seam.** No. All
   three candidates are closed; see "Why the overlay cannot host it".
2. **Every other place the product token reaches a header.** Answered above,
   and it is the reason the specification was rewritten twice. The brand list
   is a different call chain from the string, and the brand list is itself two
   call chains: the content browser client feeds the renderer and both worker
   types, while a separate `ClientHintsControllerDelegate` feeds `Sec-CH-UA`
   and `Sec-CH-UA-Full-Version-List`. Branding one and not the other is the
   inconsistency, so both are hooked or neither is.
3. **Whether changing it at all is wanted.** Split. The brand list, yes — that
   is the surface the platform provides for it and the one a person saw the
   absence of. The string, no.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the file in the checkout; commit with an owner and a reason
./tools/chromium/export-patches
./tools/check fast
```

---

## The retirement this reverses, kept as written

**Retired before it was written**, 2026-08-21, by the outcome its
own "Verify at SP-01" point 3 pre-authorised: TaffyGo keeps the upstream
user agent. The token in
`//taffy/browser/taffy_product_identity.cc` stays empty
permanently rather than provisionally. Three facts made the call:

1. The seams exist and work — SP-01 confirmed `GetProduct()` and
   `GetUserAgent()` at the pin — so nothing about feasibility decided this.
2. A changed token is compatibility-visible on every site that sniffs it,
   and the parity matrix's rendering rows (PAR-WEB) promise upstream site
   behaviour. A browser that trades site compatibility for a name in a
   header has bought a defect with a vanity string.
3. A token changed in the UA but not in `Sec-CH-UA`, the
   `navigator.userAgentData` brand list and the reduced UA
   would be a fingerprinting inconsistency, and changing all of them is a
   larger and permanently riskier delta than this spec ever budgeted.

The product identifies itself where identity belongs to the product: the
version surface (patch [0004](0004-report-upstream-provenance.md)) and the
About screen (SCR-408), which carry the Chromium base, the tag, and the
downstream delta.

**What the measurement changed.** Reason 1 was wrong about which seams matter —
the two it names do not reach the brand list at all. Reason 2 stands, and is
why hook 2 stays unexported. Reason 3 inverts: it feared a token in the string
and not in the brands, and the change now specified is a brand in the brands
and nothing in the string, which is the consistent direction rather than the
inconsistent one.

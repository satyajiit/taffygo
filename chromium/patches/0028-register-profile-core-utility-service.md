# 0028 — Register the profile core utility service

**Status:** `[Current]` — exported
**Needed by:** The sandboxed core-service boundary in decision 0037
**Estimated size:** four upstream lines across three files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/utility/services.cc` |
| Symbol | `RegisterMainThreadServices` |
| File | `//chrome/utility/BUILD.gn` |
| Symbol | `//chrome/utility:utility` dependencies |
| File | `//chrome/utility/DEPS` |
| Symbol | `include_rules` |

## The change

Register `taffy::RegisterUtilityMainThreadServices` with Chrome's utility-main
service factory and link `//taffy/utility:registry`. The downstream registry
adds only `TaffyCoreService`; its generated Mojo interface declares
`Sandbox.kService`.

The browser profile manager remains the only launcher. It creates one lazy
remote for each regular profile and an independent ephemeral instance for each
off-the-record profile. The patch therefore adds no process-global product
state and no Android-specific startup hook.

## Why an upstream patch is required

Chrome owns the utility-process service registry. A downstream service can
implement its receiver entirely below `//taffy`, but `ServiceProcessHost`
cannot bind that receiver until the utility main thread's factory knows the
interface. One registration call and its two build-graph edges are the complete
upstream seam.

## Rebase risk

**Low.** The service-factory function is shared by Chrome's other
out-of-process services and has no Taffy-specific ordering. A rename or registry
split conflicts at the one call site.

## Verification

In a real checkout, run `gn check` for `//chrome/utility:utility`, build the
Taffy product target, and kill the launched core utility process. The browser
must retain manual navigation, reject the lost generation, and apply the
profile manager's bounded restart policy.

# core/providerauth — the subscription sign-in engine

The Android half of a provider subscription sign-in (decision
`docs/decisions/0081-provider-subscription-sign-in-is-browser-run-and-vendor-gated.md`):
the compiled flow map, the per-provider sign-in state a screen renders, and
the connect, acknowledge, decline, and cancel gestures a person has. Every
network leg, every token and every redirect lives in the browser process; what
this module holds is the state machine's visible face.

The flow map is compiled on purpose. A sign-in flow is binary behaviour —
pinned origins, a flow shape, an acknowledgement obligation — and a served
catalog must not be able to invent one. The catalog gates *enablement* only:
a vendor whose row does not offer `OAUTH`, or ships switched off, has a flow
in the binary and no way to start it.

The map is hand-kept, and so are the two documents it has to agree with: the
core's `SIGN_IN_VENDORS`, which decides whether a row may be offered at all,
and the browser's pinned configuration, which carries the origins and the
client identity. Drift between them is silent until somebody presses the
button, so `ProviderSignInFlowsParityTest` reads the first two off disk and
fails on a disagreement. The browser's is C++ and out of this module's reach;
it stays the third copy and the test says nothing about it.

**Cancellation names one exact flow.** The browser returns its minted identity
only after portable admission. `ProviderSignInEngine.cancel` sends that same
identity back; portable removal happens before native work is stopped. Events
arriving while the verdict is pending are held, then discarded on acceptance
or released on refusal, so whichever terminal wins remains the only terminal.
A lost Core Service generation aborts only that generation's native flows and
reports each exact identity as unavailable, so a visible attempt cannot outlive
the portable pending record that made cancellation authoritative.

This README is the module's authority boundary; the engine's contract is the
KDoc on `ProviderSignInEngine`.

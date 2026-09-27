# Page intelligence content component

**Status:** `[Current]` browser-process implementation of the generated BIP
contract.

This component owns the WebContents, frame/document-lifetime, bounded
observation, delta, and action-dispatch integration. Profile ownership and
Core Service supervision remain in `//taffy/browser`; the sole actor-lease and
capability ledger remains in `//taffy/components/security/browser`.

Renderer messages contain request-local identifiers only. Capability
references are registered and spent in the browser process before any command
is projected onto the renderer wire.

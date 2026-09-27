# Golden message: `action-result-verified`

**Message:** `ActionResult` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`action-result-verified.json`](./action-result-verified.json)

What this document proves:

- A verified result names a browser-owned navigation event and a fresh snapshot among its verifiers, not just the renderer acknowledgement. The schema enforces this: a `VERIFIED` code with only a renderer acknowledgement fails validation.
- The declared effect is reported as satisfied with the verifier that observed it.
- The observed page epoch differs from the one the action targeted, because a committed navigation is exactly what was expected. The result is terminal.

# Golden message: `authorized-envelope-activate-link`

**Message:** `AuthorizedActionEnvelope` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`authorized-envelope-activate-link.json`](./authorized-envelope-activate-link.json)

What this document proves:

- The digest matches the proposal's digest, which is how the browser broker proves the action it is about to dispatch is exactly the action that was authorized.
- The capability reference is present here and is consumed inside the browser process. It never travels onward to a renderer.
- The required graph revision is pinned, and the preconditions and expected effects arrive with the envelope rather than being trusted to survive elsewhere.

# Golden message: `renderer-action-command-set-text`

**Message:** `RendererActionCommand` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`renderer-action-command-set-text.json`](./renderer-action-command-set-text.json)

What this document proves:

- The mirror image of the proposal that produced it. The proposal named a value and carried none; this command carries the resolved text and names nothing. The reference was resolved and spent inside the browser process, and a command that still carried it would be handing a sandboxed process something it could present again — which is why the schema refuses that shape rather than leaving it to a reviewer.
- The command carries no capability reference, no approval receipt, no task identity and no origin policy, exactly as it does for a read. A write does not widen what a renderer holds.
- The preconditions the renderer rechecks are the ones it can actually evaluate. `EDITABLE` is among them, because a field that is not editable is a refusal (`NOT_EDITABLE`) rather than an accessibility action that quietly does nothing.
- The operation is `SET_TEXT`, which the endpoint performs as one `kSetValue` on the target through the platform accessibility path — never a DOM setter and never a synthesized key sequence (decision 0059).

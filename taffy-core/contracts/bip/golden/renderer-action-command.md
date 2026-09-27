# Golden message: `renderer-action-command`

**Message:** `RendererActionCommand` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`renderer-action-command.json`](./renderer-action-command.json)

What this document proves:

- The narrowest message in the protocol. It carries a one-use command identifier, a renderer-local target, and an operation, and that is all.
- There is no capability reference, no approval receipt, no task identifier, and no origin policy in it. A renderer that replays or forges one gains nothing beyond the frame it already controls.
- The renderer preconditions are the subset a renderer can genuinely recheck. The browser broker keeps the rest, because a compromised renderer may lie about its own state.

# Golden message: `renderer-action-command-toggle`

**Message:** `RendererActionCommand` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`renderer-action-command-toggle.json`](./renderer-action-command-toggle.json)

What this document proves:

- A toggle names the state the control must end in, and the state the endpoint must find it in. `checked: true` is the target; the `UNCHECKED` precondition is the starting point. Both are required together because the accessibility action that flips a control is a flip: performing it on a control already in the target state would move it away from what was asked for, so the endpoint refuses rather than acting.
- `TOGGLE_STATE` is the one input kind with no value reference. A boolean is a decision about a control the page already offers both values of, not content authored for it, so there is nothing for the browser to hold on the person's behalf and nothing a model could smuggle in.
- The command carries no text and no option value. The schema refuses a `TOGGLE_STATE` input that carries either, so a toggle cannot be used as a delivery vehicle for bytes the model chose.

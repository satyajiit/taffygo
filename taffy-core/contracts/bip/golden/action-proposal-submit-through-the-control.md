# Golden message: `action-proposal-submit-through-the-control`

**Message:** `ActionProposal` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`action-proposal-submit-through-the-control.json`](./action-proposal-submit-through-the-control.json)

What this document proves:

- **The target of a submission is a control, not a form.** `target_handle` names the Place order button, the role precondition says `BUTTON`, and the available-action precondition says `SUBMIT_FORM`. There is no way to express "submit this form" in this contract, because there is no node handle for a form that a person ever pressed. That is the shape decision 0059 exists to make load-bearing: going through the control means the page's own validation and its own submit handler run, and the act is one the page offered.
- The input is `NONE`. A submission has nothing to carry: whatever the fields hold was put there by earlier actions, each with its own proposal, its own approval and its own record.
- It is the commit half of a prepared effect. `PREPARED_EFFECT_UNCHANGED` names the prepare by identity and by a digest over the effect as the person saw it, and the gesture postdates the prepare on the browser's monotonic clock — the rule from decision 0022 that makes a programmatic commit impossible rather than merely unlikely.
- `NO_UNDECLARED_EGRESS` is present because a submission is the action most likely to send something somewhere. It is corroborated by the network stack rather than by the renderer, which is the component it would otherwise be exonerating.
- `NON_IDEMPOTENT`: an ambiguous outcome here is never retried automatically. A second order is not a repeat of the first.

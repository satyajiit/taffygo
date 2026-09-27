# Golden message: `action-proposal-set-text-by-reference`

**Message:** `ActionProposal` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`action-proposal-set-text-by-reference.json`](./action-proposal-set-text-by-reference.json)

What this document proves:

- A write is expressible from 0.8, and it is expressible **without the proposal's author writing the value**. The input names `val_shipping_city` and carries no text at all. The document holds no clue about what the field will contain — not its length, not its shape, not whether the person has an address on file — because a reference is a name and nothing more.
- The proposal is still a plan. It carries no capability and no approval, and it reaches the page only after the policy broker resolves the reference, mints a capability bound to this exact proposal, and builds a narrower one-use command.
- `NOT_SENSITIVE_FIELD` names `PERSONAL` and the input is labelled `PERSONAL`. A field the endpoint classified as a credential could not be the target of this shape at all: the schema refuses a credential input carrying either a value or a reference to one.
- The declared effect is `NODE_VALUE_CHANGED`, un-reserved at 0.8 for exactly this reason — an action with no declared effect cannot be verified and is refused, so a write that could not say what it expected to change would not be proposable.
- The idempotency policy is `IDEMPOTENT_WRITE`: setting a field to the same value twice leaves the same page. Submitting it does not, which is why the sibling submission golden says `NON_IDEMPOTENT`.

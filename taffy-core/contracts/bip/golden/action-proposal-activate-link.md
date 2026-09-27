# Golden message: `action-proposal-activate-link`

**Message:** `ActionProposal` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`action-proposal-activate-link.json`](./action-proposal-activate-link.json)

What this document proves:

- A proposal is a plan, not an authorization. It carries no capability and no approval; the policy broker binds it to one only after its own checks pass.
- The full node handle is the target: tab, frame, page epoch, graph revision, node identifier, and expected origin together. The node identifier alone is meaningless.
- Thirteen preconditions cover epoch, revision, lifecycle, origin, node existence, role, available action, asserted and absent states, destination, sensitivity, user interaction since the lease, and budget.
- Exactly one expected effect is declared, so the outcome is verifiable. The reason shown to a person is a local sentence, not page text.

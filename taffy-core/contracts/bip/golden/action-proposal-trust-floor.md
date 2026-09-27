# Golden message: `action-proposal-trust-floor`

**Message:** `ActionProposal` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`action-proposal-trust-floor.json`](./action-proposal-trust-floor.json)

What this document proves:

- The two destination checks added at 0.5 are ordinary preconditions, declared beside the twelve that already existed. They are checked at dispatch time by the same evaluator, in the same order, and they refuse in the same way.
- `DESTINATION_CLASS_ALLOWED` carries no operand. The table it consults is compiled into the browser, so a message can name the check but can never supply, extend, or narrow the list it reads.
- `CONTENT_TRUST_AT_LEAST` carries `min_content_trust`, and the floor names the authorship the target must not carry. Here the link sits in a forum post, so a target labelled `USER_GENERATED_CONTENT` or less trusted refuses.
- Both are declared on a proposal whose target is a link the assistant found on a page, not one a person named. The proposal still authorizes nothing: it is the plan, and the broker binds a capability to it only after its own checks pass.

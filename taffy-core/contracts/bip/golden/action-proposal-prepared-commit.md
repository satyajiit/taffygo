# Golden message: `action-proposal-prepared-commit`

**Message:** `ActionProposal` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`action-proposal-prepared-commit.json`](./action-proposal-prepared-commit.json)

What this document proves:

- A consequential effect is two acts, and this is the second. The first named the same effect, showed a person exactly what it would do, and caused nothing.
- The binding names the prepare by identity **and** by digest. Matching identity alone would let a different effect ride a confirmation the person gave to something else.
- The gesture is referenced only by the monotonic time the browser received it. No consumer parses the receipt, and the time is what carries the claim: `gesture_at_monotonic_ms` postdates `prepared_at_monotonic_ms`, so the gesture can have been a response to the prepare. A commit whose gesture came first is refused however well-formed the rest of it is.
- `NO_UNDECLARED_EGRESS` is declared beside it and carries no operand: the channels this task declared are held by the browser, and a message never supplies the list it is checked against.
- The action class here is one no ratified milestone authorizes. The proposal is well-formed and would still be refused today, which is the point — the shape exists before the authority does.

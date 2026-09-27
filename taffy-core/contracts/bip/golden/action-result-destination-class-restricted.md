# Golden message: `action-result-destination-class-restricted`

**Message:** `ActionResult` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`action-result-destination-class-restricted.json`](./action-result-destination-class-restricted.json)

What this document proves:

- A destination the task's own scope never named, in a class that is reached inside a live authenticated session, refuses before dispatch. `dispatched` is false and `verified_by` is empty, which is the shape every pre-dispatch refusal takes.
- The refusal names the class, not the reason the class was consulted, and never which value or which rule matched. A page that could tell one refusal from another would have an oracle for what the check holds.
- The code is appended to the taxonomy rather than inserted into it, so the codes already written into stored records keep the numbers they had.
- Nothing here says the destination is unreachable. A person may browse there; the check constrains only what the assistant proposes on its own.

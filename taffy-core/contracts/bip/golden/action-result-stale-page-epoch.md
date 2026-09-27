# Golden message: `action-result-stale-page-epoch`

**Message:** `ActionResult` from [`action.schema.json`](../schema/action.schema.json)  
**Document:** [`action-result-stale-page-epoch.json`](./action-result-stale-page-epoch.json)

What this document proves:

- The page changed between observation and dispatch, so the action failed closed before any side effect. `dispatched` is false.
- `verified_by` is empty, which is only valid for a result that never reached verification.
- The declared effect is reported as unsatisfied rather than omitted. A task may observe again and propose a new action; it may not retry this handle, resolve the target by selector, text, or coordinates, or reuse the old approval.

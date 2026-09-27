# Golden message: `protocol-info`

**Message:** `ProtocolInfo` from [`protocol-info.schema.json`](../schema/protocol-info.schema.json)  
**Document:** [`protocol-info.json`](./protocol-info.json)

What this document proves:

- An endpoint states what it supports before anyone asks it for anything: adapters, observation scopes, action types, redaction features, and its enforced limits.
- `supported_action_types` lists only advertised action types. The four form-action values reserved for milestone M5 are absent, which is what the schema's `not` constraint enforces and what a production endpoint must do.
- Every limit is present as a field and every value here is fixture data, chosen to exercise the shape. The real bounds are a device-floor and page-corpus measurement owned by [Open (OD-031)]; nothing in this file is a target.

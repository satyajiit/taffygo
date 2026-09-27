# Golden message: `snapshot-redacted-password-field`

**Message:** `PageSnapshot` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-redacted-password-field.json`](./snapshot-redacted-password-field.json)

What this document proves:

- A password field is described structurally and never revealed. Its value descriptor is `SECRET_WITHHELD` with `redacted` true and no normalized value, which the schema enforces rather than merely recommending.
- The field is classified `CREDENTIAL` from the form type, the autocomplete token, and the label together, and it offers no actions at all. `actions` is absent rather than written as an empty list: the schema marks it `x-bip-absent-as-empty`, so the two spellings are one statement and the canonical encoding is the shorter one.
- The committed URL is disclosed as origin only. The snapshot still reports that a query string exists, so a consumer knows what was withheld without seeing it.
- The redaction summary counts what was removed and names the features that ran. It carries no sample of the removed content.

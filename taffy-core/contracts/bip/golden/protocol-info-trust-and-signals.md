# Golden message: `protocol-info-trust-and-signals`

**Message:** `ProtocolInfo` from [`protocol-info.schema.json`](../schema/protocol-info.schema.json)  
**Document:** [`protocol-info-trust-and-signals.json`](./protocol-info-trust-and-signals.json)

What this document proves:

- An endpoint advertises `CONTENT_TRUST_LABELLING` and `INJECTION_SIGNAL_DETECTION` among its redaction features, so a broker can require them before it asks for a snapshot at all.
- This is the mechanism that keeps two optional wire fields from being a fail-open. An older reader ignores a field it does not know, which is correct for compatibility and wrong for a security signal; a policy that needs labelling names a feature instead, and an endpoint that cannot run it refuses the request rather than returning unlabelled evidence that looks complete.
- Advertising is still not authorization. The endpoint lists what it can produce; every action it might later be asked to perform continues to need a capability, an actor lease, and where policy says so an approval.

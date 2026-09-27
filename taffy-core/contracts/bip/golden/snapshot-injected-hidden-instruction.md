# Golden message: `snapshot-injected-hidden-instruction`

**Message:** `PageSnapshot` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-injected-hidden-instruction.json`](./snapshot-injected-hidden-instruction.json)

What this document proves:

- One page carries text from three different authors, and the snapshot says so per run. The article paragraph is `FIRST_PARTY_DOCUMENT`, the reader comment beneath it is `USER_GENERATED_CONTENT`, and the advertisement frame is `THIRD_PARTY_EMBEDDED`. Nothing about the three is distinguishable from their sensitivity, which is `NOT_SENSITIVE` for all of them — that is the point of carrying two axes rather than one.
- The concealed instruction is **carried, not dropped**. It arrives labelled `HIDDEN_BY_STYLE`, `ZERO_WIDTH_CHARACTERS` and `IMPERATIVE_INSTRUCTION_SHAPE`, and asserts `NOT_VISIBLE` rather than omitting a visibility state. Dropping the run would leave a run unable to tell a person what the page attempted; suppressing it silently would be indistinguishable from a page that attempted nothing.
- Frame authorship is browser-owned. `frame_ad` is `is_cross_origin_to_parent` and carries `THIRD_PARTY_EMBEDDED` on the frame itself, so the label on its nodes does not depend on the renderer being honest about who wrote them.
- `lowest_content_trust` is the join over everything included, so a context engine can read one field instead of walking the graph, and reads the least trusted answer rather than an average.
- The warnings attribute each finding to the node it came from, using a bounded `detail_code` from a local vocabulary. No warning carries page text.
- The redaction summary names `CONTENT_TRUST_LABELLING` and `INJECTION_SIGNAL_DETECTION` among its applied features. An endpoint that cannot run them refuses the request instead, which is what stops an optional field from becoming a silent fail-open.

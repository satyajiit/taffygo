# Golden message: `snapshot-kernel-payload-source`

The identifier is retained for fixture compatibility. The retired in-browser
kernel and its handwritten observation envelope no longer exist; the current
consumer is the isolated Core Service.

**Message:** `PageSnapshot` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-kernel-payload-source.json`](./snapshot-kernel-payload-source.json)

What this document proves:

- The snapshot the browser projects toward the isolated Rust core is a
  `PageSnapshot` of this contract and nothing invented beside it.
- Which graph fields cross as values, which cross only as counts, and which are
  withheld. Generated Core Service fields carry the surrounding observation
  metadata; the bounded graph body below carries only BIP semantics.
- A credential node crosses marked and unnamed. `n2`'s name is in this document and is not in the bytes, which is the withholding rule stated as an instance rather than as prose.
- A cross-origin child frame is listed and excluded rather than omitted, so the page's shape stays knowable while its contents stay unread.

## Why this document exists

Every other golden document here proves a message shape. This one also anchors
the bounded graph projection implemented by
`//taffy/components/intelligence/content/bip_graph_payload.*`. The browser to
Core Service envelope is generated from the Core Service contract; there is no
second handwritten observation frame. The graph body is deliberately narrow:
it contains BIP node and edge data after browser redaction, never operation,
profile, authority, deadline, or recovery fields.

The BIP contract owns meaning, this document owns the instance, and the graph
body owns only a reviewed byte order. Decision 0039 closed OD-091 by removing
the competing outer codec.

## The instance

A two-frame page. The main frame was read. A cross-origin child was listed and
excluded, which is the shape the frame list is required to take: present and
`"included": false`, never absent, because "there is no second frame" and
"there is a second frame and its contents were not read" are different facts
about the page.

The graph carries two nodes and one edge:

| Node | Role | Sensitivity | What crosses |
|---|---|---|---|
| `n1` | `BUTTON` | `NOT_SENSITIVE` | id, frame, role, sensitivity, name `"Sign in"`, one text run, 7 text bytes |
| `n2` | `TEXT_FIELD` | `CREDENTIAL` | id, frame, role, sensitivity, value kind `SECRET_WITHHELD` — **and no name** |

`n2`'s name is `"Password"` in this document and is **not** in the bytes. The
node crosses marked `name_withheld`, which is what keeps "this node has no
name" distinguishable from "this node has a name the browser refused to
carry". The rule is fail-closed on the sensitivity: anything other than
`NOT_SENSITIVE` withholds, so `UNKNOWN_SENSITIVE` withholds too.

## The graph body this instance produces

The generated Core Service envelope carries the result code, origin,
document/frame identity, revision, byte counts, redaction summary, and frame
rows as typed fields. Only the semantic graph body uses this compact encoding.
Its field order is little-endian, with every variable-length field preceded by
its length:

| Bytes | Field |
|---|---|
| 1 | graph-body framing version (1) |
| 2 + n | `schema_version` |
| 4 | node count |
| variable | node rows: identities, role, sensitivity, flags, value kind, allowed name, text-run count and text-byte count |
| 4 | edge count |
| variable | edge rows: endpoint identities, relationship and inferred flag |

`//taffy/browser/bip_graph_payload_unittest.cc` asserts the sending-side
withholding and rescan rules.
`//taffy/test/support/bip_graph_payload_reader.cc` is a test-only strict reader
used to inspect the result; it refuses truncation, trailing bytes, and unknown
shape rather than acting as a production protocol.

## What this document does not prove

It does not prove a complete browser-to-service task or authorize an action.
It proves only the content and redaction properties of the BIP graph body; the
generated Core Service contract, effect broker, policy path, and recovery tests
own those separate claims.

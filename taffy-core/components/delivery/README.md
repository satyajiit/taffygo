# `taffy-core/components/delivery/`

The delivery domain: how TaffyGo's own artifacts reach a device after the
installer has already run.

An **asset** is a first-party artifact the product needs and the installer did
not carry — a Python standard library, a local model's weights, a tokenizer, a
filter list. It has an identity, one revision, a platform and a digest, all
decided when the product was built.

A **download** is a person's file, fetched from a page they were on and written
where they can find it. Nothing in this directory applies to one. The two share
a screen and share nothing else; `docs/decisions/0045-taffy-assets-are-a-delivery-plane.md`
section 3 is the record of why.

| Path | What it is |
|---|---|
| `core/rust/asset-plane/` | The portable policy: the compiled-in catalog, the install plan and the transfer state machine. No input or output |

The browser process performs what the plane decides, because it is the only
process that may open a socket or write a file. That split is what makes an
install replayable from an audit record, and what lets one policy serve Android,
macOS and Windows without a `cfg`.

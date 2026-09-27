# `//taffy/test/fuzz`

**Status:** `[Proposed]` fuzzing coverage for work package
WP-M2-08
**Implementation status:** `[Current]` written, **never built and never run**.
**Owns:** a fuzzer for every parser and every browser-facing renderer payload,
and the generator that seeds them from the contract.
**Does not own:** the Rust decoders. They are fuzzed beside their owning
contracts and components under `taffy-core/contracts/` and
`taffy-core/components/`, where the untrusted structure is actually walked.

## 1. Why a target exists for each of these

The protocol requires every parser and every browser-facing renderer payload to
be fuzzed. The list is therefore not a judgement call: one target per message a
renderer can send the browser, and one per pure function in
`//taffy/browser` whose input is page-derived.
TEST-INDEX.md carries the table of target to surface.

Two things a reader should not conclude from the list.

**Mojo is not the defence being tested.** Mojo validates the layout and rejects
any value outside a closed enumeration before Taffy code runs, which is exactly
why every protocol enumeration is closed. What these targets cover is what is
left afterwards: a structurally valid message whose values are hostile, and the
browser-process code that walks it.

**Deserializing is not the whole surface.** A decoder that accepts a message and
a consumer that walks it are two different attack surfaces. Every wire target
therefore goes on to convert the parts browser-process code actually reads.

## 2. The seed corpus is generated, and it fails rather than skips

Seeds have to be in the encoding the target decodes. The contract's golden
documents are JSON, which is the right seed for the graph payload — that
encoding is generated from the contract — and the wrong seed for a Mojo struct,
which decodes a binary layout. A directory of JSON handed to a Mojo decoder
would look like a seed corpus and be worth nothing: every seed rejected in its
first bytes, and the fuzzer starting from nothing.

So `write_wire_seeds` is a host tool that reads
`taffy-core/contracts/bip/golden/index.json` and `taffy-core/contracts/bip/compat/manifest.json`,
builds the corresponding message through the generated bindings, and writes the
serialized bytes.

It stops the build when it meets a golden document whose definition has no seed
builder and is not on the list of messages that never cross this boundary. That
is deliberate: a fuzzer that quietly lost its seeds is indistinguishable from a
fuzzer that found nothing.

The messages that never cross this boundary are the browser-authored ones — an
action proposal stays inside the core service between the task engine and the
policy engine, an authorized envelope stops in the browser process, and a
renderer command travels the other way. None of them is an input here, and
`WireSeedBuilder::IsBrowserFacingRendererPayload` is the single list.

## 3. What the Chromium track should check first

1. **That the seed directory is not empty.** Run the generator, look at the
   output directory. Everything else in this file is worth nothing if it is
   empty.
2. **That the compatibility fixtures survived.** Each is one deliberate
   deviation from a message that would otherwise be accepted, so each sits on a
   boundary the decoder has to get right. They make unusually good seeds and
   they are the ones most likely to be silently dropped, because the malformed
   one is not parseable JSON and takes the raw-copy path.
3. **The two targets with an assertion in the body.** `page_delta_fuzzer` and
   `origin_codec_fuzzer` assert a property rather than only exercising code: a
   delta whose epoch differs from the cursor's can never be applicable, and two
   opaque origins with different identifiers can never be equal. A failure in
   either is a real defect, not a crash to triage.
4. **The depfile.** The contract lives outside the Chromium source root, so the
   generator declares absolute inputs. Confirm a contract change rebuilds the
   seeds; if it does not, a schema change would leave the seeds describing the
   previous contract.

## 4. Related

- TEST-INDEX.md — the map from target to surface
- [`../README.md`](../README.md) — the directory's authority boundary
- Browser Intelligence Protocol
  — the verification strategy that requires these
- Threat model — adversary
  A2, the compromised renderer these targets are written against

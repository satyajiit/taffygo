# Golden request bodies

**Status:** `[Current]` — `../wire_tool_golden.rs` compares each document
against what `wire::write_request` produces today, byte for byte, and
`cargo test -p model-router` fails a tree where the two disagree.

One document per protocol family, all five of the same exchange: the person
states a goal, the model says something and calls `read_page`, the result comes
back, the model calls `click` and says nothing beside it, and that result comes
back a failure. Every request also offers one tool, so each document is a body
that could actually be sent rather than a fragment.

## Why the bytes and not the structure

Because the bytes are what a provider is sent. A comparison of parsed documents
would agree with a body whose fields had been reordered, whose numbers had
changed shape, or whose one-string turn had quietly become an array of blocks —
and none of those is a change nobody needs to see. So each file is the writer's
own output on one line, with a trailing newline the test trims.

They exist mainly to be read together. Five answers to "where does a tool
result go" sit side by side here — four different ones, since two families
share a shape and differ elsewhere — and no other place in the tree shows them
at once:

| File | Where the call goes | Where the result goes | How a failure is said |
|---|---|---|---|
| `tool-exchange-anthropic-messages.json` | a `tool_use` block inside the assistant turn | a `tool_result` block inside a turn whose role is the user's | `is_error: true` |
| `tool-exchange-openai-responses.json` | a `function_call` item with no role | a `function_call_output` item with no role | folded into the text |
| `tool-exchange-openai-completions.json` | a `tool_calls` list beside the assistant message's content | a message whose role is `tool` | folded into the text |
| `tool-exchange-google-generative-language.json` | a `functionCall` part inside the model content | a `functionResponse` part inside a user content | `response.error` instead of `response.output` |
| `tool-exchange-openai-codex-responses.json` | a `function_call` item with no role | a `function_call_output` item with no role | folded into the text |

The last two rows are the same three answers, which is the point of having
both: the subscription endpoint speaks the responses shape and differs in what
it *requires* around it — `store` false, the sealed reasoning asked for by
name, and the conversation named in `prompt_cache_key` so successive turns of
one task reach the same cached prefix. That last field is why the two documents
are not byte-identical, and it is written by this family alone.

Three of the five have no field for a failed result at all, so on those the fact
is folded into the result text with a compiled-in marker. A failure delivered
in the shape of a success is a result the model builds its next step on, which
is why it is folded rather than dropped.

## Renewing one

Read what the writer produces and put it back here — never the other way round.
A document edited to match an intention rather than an output is a test that
passes because it was told to.

```bash
cargo test -p model-router --test wire_tool_golden   # fails, printing both sides
```

## What these are not

They are hand-written published shapes, not recordings. Nothing here proves a
provider accepts the result; that is the conformance suite the
provider registry and model catalog
specifies, and it does not exist yet. The crate
[README](../../README.md) lists it under what is still missing.

Status labels used here are explained under Documentation conventions in
[AGENTS.md](../../../../../../../../AGENTS.md#documentation-conventions).

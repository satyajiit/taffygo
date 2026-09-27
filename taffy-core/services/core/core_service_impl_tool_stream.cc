// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_service_impl.h"

namespace taffy {

namespace mojom = core_service::mojom;

// A streamed tool chunk, what this seam decides about it, and where it stops.
//
// Decision 0041 section 4 says a streamed chunk leaves the browser as it
// arrives, is not buffered, and is counted in the terminal result rather than
// carried twice. `CoreSession.DeliverToolStreamChunk` is the call that carries
// it, and this is its whole implementation on the receiving side.
//
// What this method decides is everything the seam owns:
//
//   * whether this process is in a state that can receive anything at all,
//   * whether the chunk is the shape the contract describes, and
//   * whether it belongs to *this* generation of the profile service.
//
// The generation check is the one worth naming. Every other entry point on
// this interface makes it, for the same reason: a chunk minted under a
// generation the profile has left names an operation this incarnation of the
// runtime never staged and never will, and routing it anyway would let a job
// from a replaced service process reach a live task. A stale chunk is a race
// the browser cannot avoid rather than misbehaviour, so it is dropped, not
// counted and not reported.
//
// What this method does *not* do is route the chunk onward, and the reason is
// not that the call would be hard to write. It is that there is nothing on the
// other end of it. `RustCore` has no entry point for a partial answer;
// `core_runtime` has no reducer arm that could take one; and the production
// runtime stages no `EffectRequest::Tool` at all, so there is no pending tool
// operation for a chunk to belong to. Decision 0041 specifies the transport
// and says nothing about what a partial answer does to a task's state — which
// is the actual question, and one M6 owns alongside the local-model runtime
// [Open (OD-037)] names. Writing an ordered call into the core now would mean
// choosing that answer here, in a translation unit whose subject is a message
// shape, and freezing an FFI record for it that no lane in this repository can
// exercise.
//
// So a chunk that got past the checks is counted and dropped, and the count is
// what says so. It is not a metric; it is the difference between "the browser
// streamed and the core discarded it" and "the browser never streamed", which
// are otherwise the same silence. It should stay zero: the browser plans a
// streaming transport only when `ProfileToolSupervisor::SetStreamSink` has
// been given a sink, and nothing installs one, precisely because this end
// cannot yet take what it would send.
//
// This replaces a `ReportBadMessage` on the session receiver set. That call
// was wrong twice. The sender it accused is the browser process, which is this
// process's parent and the only party entitled to open a session at all — a
// utility process does not get to declare its parent a bad-message sender. And
// it did not do anything: a child process installs no
// `MojoDefaultProcessErrorHandler`, and the browser sets the per-channel
// process error callback only for the peers *it* invited, so
// `Core::NotifyBadMessage` found neither a handler for the source node nor a
// default callback and returned `MOJO_RESULT_OK` having discarded the string.
// The code read as a loud refusal and was a silent one.

void CoreServiceImpl::DeliverToolStreamChunk(mojom::ToolStreamChunkPtr chunk) {
  if (!ready_ || shutdown_started_ || !chunk || !chunk->operation ||
      !chunk->chunk || chunk->job_id.empty() || chunk->effect_id.empty() ||
      chunk->operation->service_generation != generation_) {
    return;
  }
  ++unrouted_tool_stream_chunk_count_;
}

}  // namespace taffy

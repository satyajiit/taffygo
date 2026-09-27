// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Non-browser registry rows, in appendix order.

use super::super::entry::{
    IdempotencyClass, NameMatch, ToolAvailability, ToolDispatch, ToolEntry, ToolLoading,
};
use super::super::milestone::Milestone;
use super::super::parameters;
use crate::artifact::ArtifactKind;
use crate::tool::{LibraryTool, MemoryTool, ToolRuntime};
pub(super) const ROWS: &[ToolEntry] = &[
    ToolEntry {
        name: "library.search",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M6),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::PureRead,
        dispatch: ToolDispatch::Library(LibraryTool::Search),
        terminal_safe: false,
        purpose: "Knowledge store retrieval",
        parameters: parameters::LIBRARY_SEARCH,
    },
    ToolEntry {
        name: "library.save",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M6),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Library(LibraryTool::Save),
        terminal_safe: false,
        purpose: "Explicit knowledge store capture",
        parameters: parameters::LIBRARY_SAVE,
    },
    ToolEntry {
        name: "library.remove",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M6),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Library(LibraryTool::Remove),
        terminal_safe: false,
        purpose: "Explicit knowledge store removal",
        parameters: parameters::LIBRARY_REMOVE,
    },
    ToolEntry {
        name: "memory.search",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M6),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::PureRead,
        dispatch: ToolDispatch::Memory(MemoryTool::Search),
        terminal_safe: false,
        purpose: "Active preference retrieval",
        parameters: parameters::MEMORY_SEARCH,
    },
    ToolEntry {
        name: "memory.save",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M6),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Memory(MemoryTool::Save),
        terminal_safe: false,
        purpose: "Explicitly approved preference save",
        parameters: parameters::MEMORY_SAVE,
    },
    ToolEntry {
        name: "memory.update",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M6),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Memory(MemoryTool::Update),
        terminal_safe: false,
        purpose: "Explicitly approved preference update",
        parameters: parameters::MEMORY_UPDATE,
    },
    ToolEntry {
        name: "memory.delete",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M6),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Memory(MemoryTool::Delete),
        terminal_safe: false,
        purpose: "Explicitly approved preference deletion",
        parameters: parameters::MEMORY_DELETE,
    },
    ToolEntry {
        name: "artifact.markdown.create",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Artifact(ArtifactKind::Markdown),
        terminal_safe: false,
        purpose: "Deterministic Markdown export",
        parameters: parameters::TITLED_DOCUMENT,
    },
    ToolEntry {
        name: "artifact.csv.create",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Artifact(ArtifactKind::Csv),
        terminal_safe: false,
        purpose: "Deterministic comma-separated export",
        parameters: parameters::TITLED_DOCUMENT,
    },
    ToolEntry {
        name: "artifact.xlsx.create",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M7),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Artifact(ArtifactKind::Xlsx),
        terminal_safe: false,
        purpose: "Validated spreadsheet from the current cited workspace",
        parameters: parameters::CURRENT_WORKSPACE_REPORT,
    },
    ToolEntry {
        name: "artifact.pdf.create",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M7),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Artifact(ArtifactKind::Pdf),
        terminal_safe: false,
        purpose: "Validated portable document from the current cited workspace",
        parameters: parameters::CURRENT_WORKSPACE_REPORT,
    },
    ToolEntry {
        name: "artifact.docx.create",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M7),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Artifact(ArtifactKind::Docx),
        terminal_safe: false,
        purpose: "Validated word-processing document from the current cited workspace",
        parameters: parameters::CURRENT_WORKSPACE_REPORT,
    },
    ToolEntry {
        name: "artifact.pptx.create",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M7),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Artifact(ArtifactKind::Pptx),
        terminal_safe: false,
        purpose: "Validated presentation from the current cited workspace",
        parameters: parameters::CURRENT_WORKSPACE_REPORT,
    },
    ToolEntry {
        name: "media.probe",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M7),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::PureRead,
        dispatch: ToolDispatch::ToolJob(ToolRuntime::Media),
        terminal_safe: false,
        purpose: "Read bounded duration, stream and dimension facts from a completed media download",
        parameters: parameters::MEDIA_SOURCE,
    },
    ToolEntry {
        name: "media.audio.extract",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M7),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::ToolJob(ToolRuntime::Media),
        terminal_safe: false,
        purpose: "Extract bounded linear-PCM WAVE audio from a completed media download",
        parameters: parameters::MEDIA_SOURCE,
    },
    ToolEntry {
        name: "media.frames.sample",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M7),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::ToolJob(ToolRuntime::Media),
        terminal_safe: false,
        purpose: "Sample a bounded PNG frame archive from a completed video download",
        parameters: parameters::MEDIA_FRAME_SAMPLE,
    },
    ToolEntry {
        name: "media.transcode",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M7),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::ToolJob(ToolRuntime::Media),
        terminal_safe: false,
        purpose: "Transcode a completed media download with the fixed mono WAVE preset",
        parameters: parameters::MEDIA_SOURCE,
    },
    ToolEntry {
        name: "core.table.reshape",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M7),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::PureRead,
        dispatch: ToolDispatch::Loop,
        terminal_safe: false,
        purpose: "Deterministically reshape bounded comma-separated rows in the portable core",
        parameters: parameters::TABLE_RESHAPE,
    },
    ToolEntry {
        name: "python.execute",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M7),
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::Consequential,
        dispatch: ToolDispatch::ToolJob(ToolRuntime::Python),
        terminal_safe: false,
        purpose: "Build a document or spreadsheet in the sandboxed utility process",
        parameters: parameters::PYTHON,
    },
    // The two names that reach for the person rather than for the page. They
    // are ordinary rows on purpose (decision 0054 section 6): handover is not
    // an escape hatch bolted beside the tool surface, it is what the assistant
    // reaches for once everything else has been refused, and only a row makes
    // it reachable by the same decision the model makes about every other
    // call. Taffy never learns to recognise a challenge meant to prove a
    // person is present: `BypassAccessControl` and `ExtractCredential` are
    // prohibited by class, so the challenge, the one-time code and the
    // password are all refused before anything looks at them, and handover is
    // what remains. Refusal by exhaustion has no false negatives; a detector
    // does.
    //
    // Both match `Exact`, and a `user` namespace row must never be registered
    // above them: `resolve` is a first-match scan, so a prefix row here would
    // swallow both and leave two definitions that are reviewed and never
    // reached. `no_earlier_row_shadows_a_later_name` is what says so.
    //
    // Neither is `PureRead`, which is the one class whose recovery rule
    // permits an unattended retry — a runtime free to re-ask or to hand over
    // again without consulting anybody is exactly the loop that is counted in
    // reducer state rather than discouraged in a prompt. Neither is
    // `Consequential` either: the consequential act after a handover is the
    // person's own, done by hand and attributed to them, and no tool the
    // product makes available through M4 is consequential (domain model
    // section 18.2). That leaves two, and `IdempotentWrite` is the wrong one:
    // its rule retries under the same key once the target state has been
    // checked, which presumes a target whose revision can be read back. A
    // person is not that target — the state worth checking is whether they
    // answered or acted, and re-asking is a second interruption rather than a
    // repeated write. Reconcile-first is what is left, and it is the honest
    // rule here — the person may already have answered, or may already have
    // done the thing the handover was for.
    ToolEntry {
        name: "user.handover",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::ConditionallyIdempotent,
        dispatch: ToolDispatch::Person,
        terminal_safe: false,
        // Not "a CAPTCHA, a sign-in". Decision 0188 took the CAPTCHA out of the
        // errand preamble for a measured reason — a model on the eAadhaar form
        // planned to ask for the number, read that a CAPTCHA is handed over,
        // and handed over the whole form — and left it standing here, which is
        // the copy the model reads at the moment it picks a tool. The preamble
        // is prose at the top of a prompt; this sits in the function schema
        // beside the call. On 2026-09-19 errand `7b367bd8` reached that form
        // with the CAPTCHA on screen and called `user.handover` (decision
        // 0209). This row now names the tool that does take a CAPTCHA, at the
        // one place the confusion happens.
        purpose: "Hand the page to the person for a sign-in, or when the browser will not take the request; they hand it back. For a CAPTCHA, use user.request_values instead.",
        parameters: parameters::HANDOVER,
    },
    ToolEntry {
        name: "user.ask",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::ConditionallyIdempotent,
        dispatch: ToolDispatch::Person,
        terminal_safe: false,
        purpose: "Ask the person for a value the task needs",
        parameters: parameters::ASK,
    },
    // Asking the person to fill in a form's fields (decision 0088).
    //
    // Beside `user.ask` rather than instead of it, because they carry
    // different things in different directions. An answer to `user.ask` is
    // text the model then reads — it is how the assistant learns something.
    // An answer here is never seen by the assistant at all: the person types
    // into a surface the browser owns, the browser mints each value into its
    // vault, and what comes back to this process is a count of positions.
    //
    // Conditionally idempotent for the same reason `user.handover` is: the
    // target is a person, and the state worth checking is whether they
    // answered. Re-asking is a second interruption, not a repeated write.
    ToolEntry {
        name: "user.request_values",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::ConditionallyIdempotent,
        dispatch: ToolDispatch::Person,
        terminal_safe: false,
        purpose: "Ask the person for values only they know (an ID number, an OTP); they type in TaffyGo and it goes to the site, never to you.",
        parameters: parameters::REQUEST_VALUES,
    },
    // Loop-local tools under this task's identity (decision 0009 point 4):
    // no extra assistant, no browser effect, no page lease.
    ToolEntry {
        name: "tool.search",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::PureRead,
        dispatch: ToolDispatch::Loop,
        terminal_safe: false,
        purpose: "Find deferred tools by name or purpose.",
        parameters: parameters::SEARCH_TOOLS,
    },
    ToolEntry {
        name: "tool.activate",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::IdempotentWrite,
        dispatch: ToolDispatch::Loop,
        terminal_safe: false,
        purpose: "Load one deferred tool into this task. The name must be an exact registered name.",
        parameters: parameters::ACTIVATE_TOOL,
    },
    ToolEntry {
        name: "run.spawn",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::ConditionallyIdempotent,
        dispatch: ToolDispatch::Loop,
        terminal_safe: false,
        purpose: "Run a nested pass on this task with a narrowed goal. Taffy is still the only assistant.",
        parameters: parameters::SPAWN_RUN,
    },
    ToolEntry {
        name: "device.clipboard.read",
        matching: NameMatch::Exact,
        availability: ToolAvailability::ExcludedByRequirement,
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::PureRead,
        dispatch: ToolDispatch::Unserved,
        terminal_safe: false,
        purpose: "Excluded by requirement: no tool reads the clipboard",
        parameters: parameters::NONE,
    },
    ToolEntry {
        name: "device.share.send",
        matching: NameMatch::Exact,
        availability: ToolAvailability::ExcludedByRequirement,
        loading: ToolLoading::Deferred,
        idempotency: IdempotencyClass::Consequential,
        dispatch: ToolDispatch::Unserved,
        terminal_safe: false,
        purpose: "Excluded by requirement: no tool sends shares on the user's behalf",
        parameters: parameters::NONE,
    },
];

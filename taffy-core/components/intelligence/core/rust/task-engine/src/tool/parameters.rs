// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The compiled-in parameter schema of every registered name.
//!
//! One `const` slice per shape, named for the shape rather than for the tool,
//! because several tools take the same arguments and a table per tool would be
//! the same four lines written eleven times. [`super::registry::REGISTRY`]
//! points each row at one of these.
//!
//! Two rules are worth stating before the tables, because both look like
//! omissions:
//!
//! - **Each tab operation has its own row and schema.** A shared namespace row
//!   cannot express which operands belong to open, list, activate, or close,
//!   and lets a member inherit the authority class of its neighbours.
//! - **An excluded name has no parameters.** `device.clipboard.read` and
//!   `device.share.send` are registered so that the refusal is enumerable
//!   (decision 0054), not so that they can be called. Giving either a schema
//!   would be describing the arguments of a call the product never makes.

use super::definition::{Parameter, ParameterType};

/// Where a scroll goes.
const SCROLL_DIRECTIONS: &[&str] = &["to_node"];

/// Semantic roles accepted by the bounded DOM query.
const DOM_QUERY_ROLES: &[&str] = &[
    "link", "button", "field", "heading", "list", "table", "image", "region",
];

/// Reversible states that an ordinary disclosure activation may claim.
const DISCLOSURE_STATES: &[&str] = &["expanded", "collapsed"];

/// Why the page is being handed back.
///
/// A closed list and not free text on purpose. What a person is shown at a
/// handover is composed from a trusted local template; a sentence the model
/// wrote would be model text rendered to a person at the exact moment the
/// product is admitting it cannot proceed, which is the moment it is least
/// able to check it.
const HANDOVER_REASONS: &[&str] = &[
    "refused_by_class",
    "needs_a_person",
    "nothing_left_to_try",
    "task_finished",
];

const MEMORY_SCOPES: &[&str] = &["all_tasks", "workspace"];

// Model-facing choices remain plain vocabulary. They are projected to the
// compiled worker identifiers only after validation; a dotted import-like
// spelling must never be mistaken for an arbitrary Python name.
const PYTHON_ENTRYPOINTS: &[&str] = &["document", "spreadsheet"];

/// A tool that takes nothing.
pub const NONE: &[Parameter] = &[];

/// Browser-owned navigation.
pub const NAVIGATE: &[Parameter] = &[Parameter::required(
    "address",
    ParameterType::Address,
    "The https address to open in this tab. A new site counts against the sites budget.",
)];

/// Browser-owned search.
pub const SEARCH: &[Parameter] = &[Parameter::required(
    "query",
    ParameterType::Text,
    "What to search for.",
)];

/// Opening one assistant-owned tab.
pub const TABS_OPEN: &[Parameter] = &[Parameter::required(
    "address",
    ParameterType::Address,
    "The https address the new tab starts at, a second site alongside this one.",
)];

/// One short number issued by the most recent assistant-owned tab listing.
/// It is resolved by the task-tab table, never by the DOM handle table.
pub const TASK_TAB: &[Parameter] = &[Parameter::required(
    "tab",
    ParameterType::Handle,
    "The assistant-owned tab number returned by browser.tabs.list.",
)];

/// A tool that acts on exactly one node.
pub const NODE: &[Parameter] = &[Parameter::required(
    "node",
    ParameterType::Handle,
    "The node to act on.",
)];

/// Pressing one exact node, optionally claiming a disclosure result.
///
/// Checked, selected, focused, and value states deliberately do not appear:
/// those belong to narrower action classes with their own policy treatment.
/// `expected_state` is optional because most controls are not disclosure
/// controls. Naming it buys the exact check — the browser requires the
/// opposite state before dispatch and the named one after — and requiring it
/// of every press meant an ordinary button could not be pressed at all.
pub const DOM_ACTIVATE: &[Parameter] = &[
    Parameter::required("node", ParameterType::Handle, "The control to press."),
    Parameter::optional(
        "expected_state",
        ParameterType::Choice(DISCLOSURE_STATES),
        "Only for a control that expands or collapses: the exact state it must          reach. Leave it out for an ordinary press.",
    ),
];

/// Filters applied locally to one fresh, whole-document observation.
///
/// `within` is part of the canonical query intent, but it is not projected as
/// the browser capability's executable node. The browser grants and captures
/// the document; the Rust arena applies all four filters to that observation.
pub const DOM_QUERY: &[Parameter] = &[
    Parameter::optional(
        "within",
        ParameterType::Handle,
        "Only return nodes inside this observed node.",
    ),
    Parameter::optional(
        "role",
        ParameterType::Choice(DOM_QUERY_ROLES),
        "Only return nodes with this semantic role.",
    ),
    Parameter::optional(
        "text",
        ParameterType::Text,
        "Only return nodes matching this text.",
    ),
    Parameter::optional(
        "limit",
        ParameterType::Count,
        "Return at most this many matching nodes.",
    ),
];

/// Bringing one exact node into view.
///
/// Viewport-relative up/down commands have no node identity to bind into the
/// renderer action seam or its fresh-snapshot postcondition. They therefore
/// are not offered as smaller spellings of this node-targeted operation.
pub const SCROLL: &[Parameter] = &[
    Parameter::required(
        "direction",
        ParameterType::Choice(SCROLL_DIRECTIONS),
        "Bring the named node into view.",
    ),
    Parameter::required(
        "node",
        ParameterType::Handle,
        "The node to bring into view.",
    ),
];

/// A tool that acts on one form.
pub const FORM: &[Parameter] = &[Parameter::required(
    "form",
    ParameterType::Handle,
    "The form to act on.",
)];

/// Filling one field with a value the person supplied.
///
/// The second parameter names *which* of the person's answers to use, and
/// carries no bytes. It was `Text` — a value the model composed — until
/// decisions 0063 and 0088 settled who mints a field value: a model that can
/// author the bytes entering a field has made every control below it a matter
/// of what it chose to type, and no gate further down can tell an authored
/// value from a person's.
pub const FORM_FILL: &[Parameter] = &[
    Parameter::required("field", ParameterType::Handle, "The field to fill."),
    Parameter::required(
        "value_from",
        ParameterType::SuppliedValue,
        "Which of the person's supplied values to use.",
    ),
];

/// Setting one toggle to an explicit state. A boolean is state, not a field
/// value, and cannot smuggle text across the browser-owned value boundary.
pub const FORM_TOGGLE: &[Parameter] = &[
    Parameter::required("field", ParameterType::Handle, "The toggle to set."),
    Parameter::required(
        "checked",
        ParameterType::Flag,
        "The exact checked state the control must reach.",
    ),
];

/// Submitting uses the page's exact advertised submit control, never a form
/// node reinterpreted as a generic click.
pub const FORM_SUBMIT: &[Parameter] = &[Parameter::required(
    "control",
    ParameterType::Handle,
    "The submit control the page advertised.",
)];

/// Starting a download.
pub const DOWNLOAD_START: &[Parameter] = &[Parameter::required(
    "address",
    ParameterType::Address,
    "The https address of the file to fetch, as the page offered it.",
)];

/// Knowledge store retrieval.
pub const LIBRARY_SEARCH: &[Parameter] = &[
    Parameter::required("query", ParameterType::Text, "What to look for."),
    Parameter::optional("limit", ParameterType::Count, "At most this many entries."),
];

/// Exact saved-workspace fact promotion. Every revision is bound before
/// policy so execution never guesses which mutable value the model meant.
pub const LIBRARY_SAVE: &[Parameter] = &[
    Parameter::required(
        "workspace",
        ParameterType::Text,
        "The saved workspace identity.",
    ),
    Parameter::required(
        "workspace_revision",
        ParameterType::Count,
        "The exact saved workspace revision.",
    ),
    Parameter::required(
        "fact",
        ParameterType::Text,
        "The cited fact identity to keep.",
    ),
    Parameter::required(
        "entry_revision",
        ParameterType::Count,
        "The entry revision currently observed, or zero for a new entry.",
    ),
];

/// Exact Library deletion.
pub const LIBRARY_REMOVE: &[Parameter] = &[
    Parameter::required(
        "entry",
        ParameterType::Text,
        "The exact Library entry identity.",
    ),
    Parameter::required(
        "entry_revision",
        ParameterType::Count,
        "The exact Library entry revision currently observed.",
    ),
];

/// Bounded search over one of the person's attached stores.
pub const STORE_SEARCH: &[Parameter] = &[
    Parameter::required(
        "query",
        ParameterType::Text,
        "Words to match in titles and addresses.",
    ),
    Parameter::optional("limit", ParameterType::Count, "At most this many rows."),
];

/// Bounded listing over one of the person's attached stores.
pub const STORE_LIST: &[Parameter] = &[Parameter::optional(
    "limit",
    ParameterType::Count,
    "At most this many rows.",
)];

/// Bounded active-Memory retrieval.
pub const MEMORY_SEARCH: &[Parameter] = &[
    Parameter::required("query", ParameterType::Text, "What preference to look for."),
    Parameter::optional("limit", ParameterType::Count, "At most this many records."),
];

/// Explicitly approved task suggestion saved as Memory.
pub const MEMORY_SAVE: &[Parameter] = &[
    Parameter::required(
        "statement",
        ParameterType::Text,
        "The exact preference sentence to show for approval.",
    ),
    Parameter::required(
        "scope",
        ParameterType::Choice(MEMORY_SCOPES),
        "Whether it applies to all tasks or one workspace.",
    ),
    Parameter::optional(
        "workspace",
        ParameterType::Text,
        "The exact workspace identity, required only for workspace scope.",
    ),
    Parameter::optional(
        "expires_at",
        ParameterType::Count,
        "A visible nonzero expiry time, or no expiry when omitted.",
    ),
];

/// Explicitly approved exact-revision Memory update.
pub const MEMORY_UPDATE: &[Parameter] = &[
    Parameter::required("memory", ParameterType::Text, "The exact Memory identity."),
    Parameter::required(
        "record_revision",
        ParameterType::Count,
        "The exact record revision currently observed.",
    ),
    Parameter::required(
        "statement",
        ParameterType::Text,
        "The exact replacement sentence to show for approval.",
    ),
    Parameter::required(
        "scope",
        ParameterType::Choice(MEMORY_SCOPES),
        "Whether it applies to all tasks or one workspace.",
    ),
    Parameter::optional(
        "workspace",
        ParameterType::Text,
        "The exact workspace identity, required only for workspace scope.",
    ),
    Parameter::optional(
        "expires_at",
        ParameterType::Count,
        "A visible nonzero expiry time, or no expiry when omitted.",
    ),
];

/// Explicitly approved exact-revision Memory deletion.
pub const MEMORY_DELETE: &[Parameter] = &[
    Parameter::required("memory", ParameterType::Text, "The exact Memory identity."),
    Parameter::required(
        "record_revision",
        ParameterType::Count,
        "The exact record revision currently observed.",
    ),
];

/// A document the product writes.
pub const TITLED_DOCUMENT: &[Parameter] = &[Parameter::required(
    "title",
    ParameterType::Text,
    "What to call it.",
)];

/// A rich report over the task's exact accepted workspace revision.
///
/// The call intentionally carries no document body, table cells, markup,
/// formulas, archive parts, or citation claims. Those values come from the
/// validated workspace snapshot and cross into `file-engine` through its
/// typed sourced-report request. The model chooses only the registered format
/// by choosing a tool name.
pub const CURRENT_WORKSPACE_REPORT: &[Parameter] = &[];

/// One browser-custodied completed audio or video download.
///
/// `source` is the short number returned by the task's download listing. Rust
/// resolves it to the browser's opaque download identity and exact browser
/// session before a proposal exists; it is never a path, URL, or byte string.
pub const MEDIA_SOURCE: &[Parameter] = &[Parameter::required(
    "source",
    ParameterType::Handle,
    "The completed audio or video number returned by browser.download.list.",
)];

/// One exact task-started download. Listed page/manual downloads do not gain
/// this authority merely because they received a short display handle.
pub const TASK_DOWNLOAD: &[Parameter] = &[Parameter::required(
    "download",
    ParameterType::Handle,
    "The task-started download number returned by browser.download.start.",
)];

/// One browser-custodied media source and a bounded number of sampled frames.
pub const MEDIA_FRAME_SAMPLE: &[Parameter] = &[
    Parameter::required(
        "source",
        ParameterType::Handle,
        "The completed video number returned by browser.download.list.",
    ),
    Parameter::optional(
        "max_frames",
        ParameterType::Count,
        "At most twelve evenly sampled frames; six when omitted.",
    ),
];

/// A bounded table and one closed native reshape recipe.
///
/// Both values remain transient model-call operands. The recipe is parsed
/// into `table-engine`'s closed Rust types; it cannot name code, a file, a
/// module, an address, or a worker entrypoint.
const TABLE_RECIPE_DESCRIPTION: &str = concat!(
    "A JSON object with an operations array. Closed operation shapes: ",
    "select {op,columns}; order {op,by:[{column,direction,mode}]}; ",
    "filter {op,column,predicate,value,mode}; ",
    "group {op,by,aggregates:[{column,as,function,mode}]}; ",
    "pivot {op,rows,column,value,aggregate,mode}; ",
    "dedupe {op,columns,keep}; limit {op,rows}. ",
    "Directions are ascending or descending; modes are text or decimal; ",
    "predicates are equal, not_equal, less_than, less_than_or_equal, greater_than, ",
    "greater_than_or_equal, contains, starts_with, is_empty, or is_not_empty; ",
    "aggregate functions are count, sum, minimum, or maximum; keep is first or last. ",
    "Mode is optional and defaults to text. Sum requires decimal mode; value is omitted only ",
    "for is_empty and is_not_empty."
);

pub const TABLE_RESHAPE: &[Parameter] = &[
    Parameter::required(
        "rows",
        ParameterType::Text,
        "UTF-8 RFC 4180 comma-separated rows with CRLF records and one unique, non-empty header row.",
    ),
    Parameter::required(
        "recipe",
        ParameterType::Text,
        TABLE_RECIPE_DESCRIPTION,
    ),
];

/// One compiled Python operation over two bounded declarative values.
///
/// There is deliberately no source, module, path, argv, import, or package
/// parameter. The registered entrypoint selects frozen code in the utility
/// binary; the core converts the two transient values to its exact JSON body.
pub const PYTHON: &[Parameter] = &[
    Parameter::required(
        "entrypoint",
        ParameterType::Choice(PYTHON_ENTRYPOINTS),
        "The compiled document or spreadsheet operation to run.",
    ),
    Parameter::required("title", ParameterType::Text, "The document or sheet title."),
    Parameter::required(
        "content",
        ParameterType::Text,
        "Plain document text, or CSV including its header row.",
    ),
];

/// Handing the page back to the person.
pub const HANDOVER: &[Parameter] = &[Parameter::required(
    "reason",
    ParameterType::Choice(HANDOVER_REASONS),
    "Why the assistant is stopping here.",
)];

/// Asking the person for something the task needs.
pub const ASK: &[Parameter] = &[Parameter::required(
    "subject",
    ParameterType::Text,
    "What the task needs from the person.",
)];

/// Asking the person to fill in the fields of a form only they can answer
/// (decision 0088).
///
/// One handle, and deliberately not a list of fields. The model names the
/// *form*; the browser decides which of its fields need a person and what kind
/// of thing each one is, because the browser is already the authority on a
/// field's classification — it re-reads it from the node at dispatch and
/// believes nothing the proposal asserts about it.
///
/// Letting the model enumerate the fields would make it the author of that
/// judgement, and a model that could name which fields need a person could
/// also name which do not. It would also be the second place a field's class
/// is decided, and the two would disagree exactly when it mattered.
///
/// A field that belongs to no form is named directly, and the browser takes
/// it as the request when it is one a person must fill (`FieldNeedsAPerson`
/// in `//taffy/browser`), together with the page's other fields only the
/// person can supply, which the core names beside it (decision 0238) — so the
/// description does not tell the model to ask once per field. The judgement is
/// the browser's either way; the model only names the node, and most sites
/// draw their inputs with no form element around them.
pub const REQUEST_VALUES: &[Parameter] = &[Parameter::required(
    "form",
    ParameterType::Handle,
    "The form whose fields the person needs to fill in, or one field when it is in no form; the page's other fields only the person can supply are asked for with it.",
)];

/// Finding deferred tools by name or purpose.
pub const SEARCH_TOOLS: &[Parameter] = &[Parameter::required(
    "query",
    ParameterType::Text,
    "Find deferred tools by name or purpose.",
)];

/// Loading one deferred tool into this task.
pub const ACTIVATE_TOOL: &[Parameter] = &[Parameter::required(
    "name",
    ParameterType::Text,
    "Load one deferred tool into this task. The name must be an exact registered name.",
)];

/// Starting a nested pass under this task's identity.
pub const SPAWN_RUN: &[Parameter] = &[Parameter::required(
    "goal",
    ParameterType::Text,
    "Run a nested pass on this task with a narrowed goal. Taffy is still the only assistant.",
)];

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The bounded, transient store for one observed page.

mod text_search;

use std::collections::BTreeMap;

use bip_types::action::ActionType;
use bip_types::identity::{
    FrameId, GraphRevision, NodeHandle, Origin, PageEpoch, SemanticNodeId, TabId,
};
use bip_types::snapshot::{ContentSignal, ContentTrust, NodeState, SemanticRole, Sensitivity};
use policy_engine::origin::{normalize_serialization, OriginError};

use task_engine::action::DomQueryRole;
use task_engine::observation::PageObservationEvidence;

use self::text_search::contains_folded;

trait ContainsWorkRecorder {
    fn record_index_build_visit(&mut self) {}
    fn record_ordered_lookup(&mut self) {}
}

struct IgnoreContainsWork;

impl ContainsWorkRecorder for IgnoreContainsWork {}

#[derive(Clone, Copy)]
struct ContainsIndexEntry {
    first_position: usize,
    is_form: bool,
}

#[cfg(test)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
struct ContainsWorkStats {
    index_build_visits: usize,
    ordered_lookups: usize,
}

#[cfg(test)]
impl ContainsWorkRecorder for ContainsWorkStats {
    fn record_index_build_visit(&mut self) {
        self.index_build_visits += 1;
    }

    fn record_ordered_lookup(&mut self) {
        self.ordered_lookups += 1;
    }
}

/// Nodes one arena will hold, matching the bound the core-service contract puts
/// on a single task observation. A payload cannot legitimately exceed it,
/// because the same bound refused it one layer down.
pub const MAX_ARENA_NODES: usize = core_service_types::MAX_TASK_OBSERVATION_NODES;

/// Text bytes one arena will hold across every node, matching the contract's
/// own total for an observation.
pub const MAX_ARENA_TEXT_BYTES: usize = core_service_types::MAX_TASK_OBSERVATION_TEXT_BYTES;

/// Containment pairs one bounded arena can retain.
///
/// Linear in the node ceiling rather than square. The square is the true worst
/// case — node identifiers are unique, so there cannot be more distinct
/// parent/child pairs than that — and it was a usable bound while the node
/// ceiling was 128, where the square is sixteen thousand. It is not a bound at
/// all at the ceiling this now has: 1500 squared is 2.25 million retained
/// pairs, which is not a transient page fact, and a number that large stops
/// being a guard and becomes a comment.
///
/// Containment is a tree relation in every graph the renderer emits: each node
/// is contained by at most one parent, so a complete document contributes
/// fewer than one pair per node. The multiplier is headroom for a graph that
/// repeats a parent rather than an allowance for a shape nothing produces, and
/// the count is a ceiling on retention, not a preallocation — passing it stops
/// recording pairs and clears `contains_complete`, which is what a consumer
/// reads to know the containment view is partial.
pub const MAX_ARENA_CONTAINS_EDGES: usize = MAX_ARENA_NODES * 4;

/// What class of place a node's destination leads to, and never the place.
///
/// Four bits arrive on the wire and no URL does — not the origin, not the path,
/// not a digest of either. What a consumer needs in order to decide whether
/// following a link is the same kind of act as scrolling is the class of place,
/// and this is that class.
///
/// Held as the wire byte rather than as four fields. The bits are the framing's
/// own and this type is the only reader of them, so unpacking into booleans
/// would create a second representation to keep in step with the first for no
/// gain — and the question a caller asks is always one bit at a time anyway.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DestinationClass(u8);

impl DestinationClass {
    const PRESENT: u8 = 1 << 0;
    const CROSS_ORIGIN: u8 = 1 << 1;
    const OPENS_NEW_TAB: u8 = 1 << 2;
    const IS_DOWNLOAD: u8 = 1 << 3;

    /// Builds a class from the framing's own byte. The caller has already
    /// refused any bit this build does not know.
    pub const fn from_bits(bits: u8) -> Self {
        Self(bits)
    }

    /// The node leads somewhere at all.
    pub const fn present(self) -> bool {
        self.0 & Self::PRESENT != 0
    }

    /// Somewhere on another origin.
    pub const fn cross_origin(self) -> bool {
        self.0 & Self::CROSS_ORIGIN != 0
    }

    /// Somewhere that opens in a new tab.
    pub const fn opens_new_tab(self) -> bool {
        self.0 & Self::OPENS_NEW_TAB != 0
    }

    /// Somewhere that starts a download rather than a navigation.
    pub const fn is_download(self) -> bool {
        self.0 & Self::IS_DOWNLOAD != 0
    }
}

/// Why an observation could not be turned into a page identity.
///
/// Two variants and not one, because they are two different statements about
/// the browser. `Origin` says the string was not an origin at all; the second
/// says it was a well-formed origin written a way this core would not have
/// written it — `HTTPS://Example.test` rather than `https://example.test`. The
/// second is the one worth naming separately: it is the shape a comparison
/// against `expected_origin` would answer wrongly on, and folding it into
/// "malformed" would lose exactly the case that motivates the check.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PageIdentityError {
    /// The browser's origin was not a well-formed origin.
    Origin(OriginError),
    /// It parsed, but is not the canonical serialization of what it parsed to.
    OriginNotCanonical,
}

/// Which page an arena holds, as the five facts a handle freezes besides the
/// node's own identifier.
///
/// They are page facts and not node facts, and keeping them here rather than
/// on every [`ArenaNode`] is what makes that true in the type. The graph
/// decoder already refuses a payload whose node rows disagree with the
/// envelope about the frame, so one payload is one frame of one page at one
/// revision; storing the tuple per node would invite a later change to let
/// those drift apart silently, which is the whole failure the decoder's frame
/// check exists to stop.
///
/// `Eq` is absent because [`Origin`] does not have it: an opaque origin is
/// never equal to itself, and that is the correct answer rather than a gap to
/// be papered over — two opaque origins being distinct is what stops a
/// sandboxed frame inheriting another's reach.
#[derive(Clone, Debug, PartialEq)]
pub struct PageIdentity {
    /// The tab the page is in.
    pub tab_id: TabId,
    /// The frame within it, in the browser's identifier space.
    pub frame_id: FrameId,
    /// The document generation.
    pub page_epoch: PageEpoch,
    /// The revision the renderer stated for this graph.
    pub graph_revision: GraphRevision,
    /// The origin an action against these nodes is expected to reach.
    pub expected_origin: Origin,
}

impl PageIdentity {
    /// The page one observation was of, or why the core will not vouch for it.
    ///
    /// The origin is put back through `normalize_serialization` and required
    /// to round-trip, which is the same gate consent previews and start-task
    /// decoding already apply to a browser-supplied origin. It matters more
    /// here than there: this string becomes a handle's `expected_origin`, and
    /// an expected origin is what a later action is checked against. Accepting
    /// a serialization the core cannot re-derive would put a value nothing
    /// canonicalised on the security side of that comparison.
    pub fn from_evidence(evidence: &PageObservationEvidence) -> Result<Self, PageIdentityError> {
        let origin = normalize_serialization(&evidence.normalized_origin)
            .map_err(PageIdentityError::Origin)?;
        if origin.display() != evidence.normalized_origin {
            return Err(PageIdentityError::OriginNotCanonical);
        }
        Ok(Self {
            tab_id: evidence.tab_id.clone(),
            frame_id: evidence.frame_id.clone(),
            page_epoch: evidence.page_epoch.clone(),
            graph_revision: GraphRevision(evidence.graph_revision),
            expected_origin: origin.to_wire(),
        })
    }

    /// The full handle for one of this page's nodes.
    ///
    /// The six facts a stale-node check compares are frozen here, at the
    /// moment the node is offered, so that check is a comparison rather than
    /// an inference about what the world looked like earlier.
    pub fn node_handle(&self, node_id: &str) -> NodeHandle {
        NodeHandle::new(
            self.tab_id.clone(),
            self.frame_id.clone(),
            self.page_epoch.clone(),
            self.graph_revision,
            SemanticNodeId::new(node_id),
            self.expected_origin.clone(),
        )
    }
}

/// One node, as much of it as was allowed to cross.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ArenaNode {
    /// The core's own identifier for the node. It never reaches a model
    /// (decision 0053 section 3); it is what a resolved handle points at.
    pub node_id: String,
    /// What kind of thing the node is.
    pub role: SemanticRole,
    /// What the browser classified it as.
    pub sensitivity: Sensitivity,
    /// The node's label, if one crossed. `None` covers both "no label" and
    /// "label withheld"; `name_withheld` is what tells them apart.
    pub name: Option<String>,
    /// The node had a label and this payload does not carry it.
    pub name_withheld: bool,
    /// What the node can be asked to do. This grants nothing: it is a claim
    /// about what the element would respond to, and every one of these still
    /// has to be authorized and minted as a capability before anything happens.
    pub actions: Vec<ActionType>,
    /// The states that were allowed to cross.
    pub states: Vec<NodeState>,
    /// Where the node leads, as a class.
    pub destination: DestinationClass,
    /// Who authored the node-level content. An absent wire label has already
    /// become `UnknownUntrusted` before a node reaches the arena.
    pub content_trust: ContentTrust,
    /// Canonical content-shape signals attached to the node as a whole.
    pub content_signals: Vec<ContentSignal>,
    /// The runs of text that crossed, in document order.
    pub text: Vec<ArenaTextRun>,
    /// The node had text and this payload does not carry all of it.
    pub text_withheld: bool,
    /// How many runs the page had, whatever arrived. Kept beside `text` rather
    /// than derived from it, because the gap between the two is the whole of
    /// what a consumer can say about what it was not told.
    pub declared_text_runs: u32,
    /// How many bytes those runs held, on the same terms.
    pub declared_text_bytes: u64,
    /// The form this node belongs to, as that form's own `node_id`.
    ///
    /// Set from a `CONTAINS` edge whose parent is a form region — a `Region`
    /// that leads somewhere. The form adapter is the one that writes that
    /// shape. A landmark region has the role and no destination, so its
    /// children are not a form. The identifier never reaches a model; the
    /// projection turns it into the form's handle.
    pub container: Option<String>,
}

/// One text run with the authorship and content-shape evidence that travelled
/// with its bytes.
///
/// Keeping the three in one value is the propagation rule in the type: a
/// caller cannot reorder text independently of its label or retain the text
/// while accidentally dropping the signals.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ArenaTextRun {
    /// Bounded, redacted page text.
    pub text: String,
    /// Who authored these exact bytes.
    pub content_trust: ContentTrust,
    /// Canonical signals observed on this exact run.
    pub content_signals: Vec<ContentSignal>,
}

impl ArenaNode {
    /// Whether anything a reader could act on crossed for this node.
    ///
    /// A label or a run of text. Not an action or a state: those say a control
    /// exists and nothing about what it is for, and a page made entirely of
    /// unlabelled controls is one nobody can be asked to choose from.
    pub fn carries_content(&self) -> bool {
        self.name.is_some() || !self.text.is_empty()
    }

    /// Whether anything was kept from this node.
    pub const fn withheld_anything(&self) -> bool {
        self.name_withheld || self.text_withheld
    }
}

/// Whether a page could be read, told apart from a page with nothing in it.
///
/// The distinction is not a nicety. A projection that came back empty because
/// every node was withheld looks exactly like a projection of a blank page, and
/// a consumer handed the second conclusion acts on it: it reports that the page
/// holds nothing, or it stops looking for a field that is there. Reaching this
/// verdict from the node count and the byte count — facts the payload carries
/// whatever it withheld — is what keeps the two apart.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Readability {
    /// Content crossed. The page can be read.
    Readable,
    /// The graph was empty or held nothing but structure. The page really is
    /// blank, and reporting so is correct.
    Empty,
    /// The graph was not trivial and no content crossed: there was something
    /// here and this build was not allowed to see it. Carries what is known
    /// about the size of what was refused.
    Unreadable {
        /// Nodes the graph held.
        node_count: usize,
        /// Bytes of text the page declared, across every node.
        text_bytes: u64,
    },
}

/// A bounded, already-redacted DOM query result.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DomQueryProjection {
    // Positions in the source arena, still in document order. A staged query
    // is a transient view over that arena, not a second owner of its strings.
    // `LivePage` drops this projection before replacing the source arena, so
    // the positions cannot outlive or drift from the nodes they name.
    positions: Vec<usize>,
    /// All matches before the requested result limit was applied.
    pub matched: usize,
    /// Whether the retained graph and visible text support an exhaustive answer.
    pub complete: bool,
}

impl DomQueryProjection {
    /// Matching nodes retained after the requested result limit.
    pub fn retained(&self) -> usize {
        self.positions.len()
    }

    /// Whether the bounded view retained no visible match.
    pub fn is_empty(&self) -> bool {
        self.positions.is_empty()
    }

    /// Borrows the matching nodes from the arena that produced this result.
    ///
    /// Invalid positions are refused rather than indexed. They cannot occur
    /// through [`PageArena::query`], and this keeps the invariant fail-closed
    /// if a later refactor ever lets the arena and its view drift apart.
    pub(crate) fn nodes<'a>(
        &'a self,
        arena: &'a PageArena,
    ) -> impl Iterator<Item = &'a ArenaNode> + Clone {
        self.positions
            .iter()
            .filter_map(|position| arena.nodes.get(*position))
    }

    pub(crate) fn readability(&self, arena: &PageArena) -> Readability {
        readability_for(self.nodes(arena))
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PageArena {
    nodes: Vec<ArenaNode>,
    text_bytes: usize,
    children: Vec<Vec<usize>>,
    contains_count: usize,
    contains_complete: bool,
}

impl Default for PageArena {
    fn default() -> Self {
        Self::new()
    }
}

impl PageArena {
    /// An empty arena.
    pub const fn new() -> Self {
        Self {
            nodes: Vec::new(),
            text_bytes: 0,
            children: Vec::new(),
            contains_count: 0,
            contains_complete: true,
        }
    }

    /// Adds one node, or refuses because a bound is spent.
    ///
    /// Refusing rather than trimming is the same choice made everywhere else on
    /// this path: an arena silently short of the page it claims to hold is one
    /// whose emptiness means nothing, and [`Readability`] is built on being able
    /// to trust exactly that.
    pub fn push(&mut self, node: ArenaNode) -> bool {
        if self.nodes.len() >= MAX_ARENA_NODES {
            return false;
        }
        let bytes: usize = node.text.iter().map(|run| run.text.len()).sum();
        let Some(total) = self.text_bytes.checked_add(bytes) else {
            return false;
        };
        if total > MAX_ARENA_TEXT_BYTES {
            return false;
        }
        self.text_bytes = total;
        self.nodes.push(node);
        true
    }

    /// Records form membership from `CONTAINS` edges whose parent is a form.
    ///
    /// A self-edge is ignored. A child already in a form keeps the first one:
    /// two parents would be two forms claiming the same control, and picking
    /// either later would be a guess with a handle behind it.
    pub fn apply_contains(&mut self, edges: &[(&str, &str)]) {
        self.apply_contains_recording(edges, &mut IgnoreContainsWork);
    }

    fn apply_contains_recording<W: ContainsWorkRecorder>(
        &mut self,
        edges: &[(&str, &str)],
        work: &mut W,
    ) {
        self.children = vec![Vec::new(); self.nodes.len()];
        self.contains_count = 0;
        self.contains_complete = true;
        let mut node_index = BTreeMap::new();
        for (position, node) in self.nodes.iter().enumerate() {
            work.record_index_build_visit();
            // The old linear `find` selected the first duplicate identifier.
            // Preserve that exact choice instead of letting a later duplicate
            // overwrite its position in the index.
            let entry = node_index
                .entry(node.node_id.as_str())
                .or_insert(ContainsIndexEntry {
                    first_position: position,
                    is_form: false,
                });
            // Form membership was identifier-wide: any form-shaped duplicate
            // made the parent identifier valid. Keep that validation separate
            // from the first-node position above.
            if node.role == SemanticRole::Region && node.destination.present() {
                entry.is_form = true;
            }
        }
        let mut form_parents = vec![None; self.nodes.len()];
        for (parent, child) in edges {
            if parent == child {
                continue;
            }
            work.record_ordered_lookup();
            let Some(parent_entry) = node_index.get(*parent).copied() else {
                continue;
            };
            work.record_ordered_lookup();
            let Some(position) = node_index.get(*child).map(|entry| entry.first_position) else {
                continue;
            };
            if self.contains_count < MAX_ARENA_CONTAINS_EDGES {
                if let Some(children) = self.children.get_mut(parent_entry.first_position) {
                    children.push(position);
                    self.contains_count = self.contains_count.saturating_add(1);
                }
            } else {
                self.contains_complete = false;
            }
            if !parent_entry.is_form {
                continue;
            }
            if let Some(slot) = form_parents.get_mut(position) {
                if slot.is_none() {
                    *slot = Some(*parent);
                }
            }
        }
        drop(node_index);
        for (node, parent) in self.nodes.iter_mut().zip(form_parents) {
            node.container = parent.map(str::to_owned);
        }
    }

    #[cfg(test)]
    fn apply_contains_counted(&mut self, edges: &[(&str, &str)]) -> ContainsWorkStats {
        let mut work = ContainsWorkStats::default();
        self.apply_contains_recording(edges, &mut work);
        work
    }

    /// The nodes, in the order the page presented them.
    pub fn nodes(&self) -> &[ArenaNode] {
        &self.nodes
    }

    /// Text bytes actually held.
    pub const fn text_bytes(&self) -> usize {
        self.text_bytes
    }

    /// Applies a query without letting its text or selectors leave the core.
    ///
    /// Matching is over the bytes that already crossed the browser's
    /// sensitivity gate. A withheld candidate therefore makes the result
    /// explicitly incomplete; it never turns missing authority into a false
    /// "no matches" conclusion.
    pub fn query(
        &self,
        within: Option<&str>,
        role: Option<DomQueryRole>,
        text: Option<&str>,
        limit: usize,
    ) -> DomQueryProjection {
        let (scope, scope_root_found) = self.query_scope(within);
        let folded_text = text.map(str::to_lowercase);
        // Keep only what the caller may render. The old projection first held
        // every matching position and then cloned each retained node (including
        // its strings and nested vectors) into a second PageArena. A query is a
        // view over an arena that remains resident, so positions are sufficient.
        let mut positions = Vec::with_capacity(limit.min(self.nodes.len()));
        let mut matched = 0_usize;
        let mut withheld_candidate = false;
        for (position, node) in self.nodes.iter().enumerate() {
            if scope
                .as_ref()
                .is_some_and(|included| !included.get(position).copied().unwrap_or(false))
                || !role_matches(role, node.role)
            {
                continue;
            }
            if let Some(needle) = folded_text.as_deref() {
                if node.withheld_anything() {
                    withheld_candidate = true;
                }
                if !node_text_matches(node, needle) {
                    continue;
                }
            }
            matched = matched.saturating_add(1);
            if positions.len() < limit {
                positions.push(position);
            }
        }

        DomQueryProjection {
            positions,
            matched,
            complete: self.contains_complete
                && scope_root_found
                && !withheld_candidate
                && matched <= limit,
        }
    }

    fn query_scope(&self, within: Option<&str>) -> (Option<Vec<bool>>, bool) {
        let Some(node_id) = within else {
            // No scope root means every node is eligible. Representing that as
            // absence avoids allocating and filling a page-sized bit vector on
            // the common unscoped-query path.
            return (None, true);
        };
        let mut included = vec![false; self.nodes.len()];
        let Some(root) = self.nodes.iter().position(|node| node.node_id == node_id) else {
            // An absent requested root is not proof that its subtree has no
            // matches. It can mean the fresh bounded observation omitted or
            // retired the target, so the caller must see an incomplete query
            // instead of a complete empty answer.
            return (Some(included), false);
        };
        if let Some(value) = included.get_mut(root) {
            *value = true;
        }
        let mut pending = vec![root];
        while let Some(parent) = pending.pop() {
            let Some(row) = self.children.get(parent) else {
                continue;
            };
            for child in row {
                if included.get(*child).copied().unwrap_or(false) {
                    continue;
                }
                if let Some(value) = included.get_mut(*child) {
                    *value = true;
                    pending.push(*child);
                }
            }
        }
        (Some(included), true)
    }

    /// Whether this page could be read, and how that is known.
    pub fn readability(&self) -> Readability {
        readability_for(self.nodes.iter())
    }

    /// Drops everything. Called when the turn that asked for this page ends.
    ///
    /// The capacity goes with it. Keeping an allocation warm would leave page
    /// bytes in a buffer nothing reads and nothing clears, which is the state
    /// this type exists to make impossible to be in by accident.
    pub fn clear(&mut self) {
        self.nodes = Vec::new();
        self.text_bytes = 0;
        self.children = Vec::new();
        self.contains_count = 0;
        self.contains_complete = true;
    }
}

fn readability_for<'a>(nodes: impl Iterator<Item = &'a ArenaNode>) -> Readability {
    let mut node_count = 0_usize;
    let mut withheld_anything = false;
    let mut declared_bytes = 0_u64;
    for node in nodes {
        node_count = node_count.saturating_add(1);
        if node.carries_content() {
            return Readability::Readable;
        }
        withheld_anything |= node.withheld_anything();
        declared_bytes = declared_bytes.saturating_add(node.declared_text_bytes);
    }
    // A page is only honestly empty when there was nothing to withhold. If
    // the graph declared text, or if it is large enough that a blank document
    // would not have produced it, then something was here.
    if declared_bytes == 0 && !withheld_anything {
        return Readability::Empty;
    }
    Readability::Unreadable {
        node_count,
        text_bytes: declared_bytes,
    }
}

const fn role_matches(expected: Option<DomQueryRole>, actual: SemanticRole) -> bool {
    match expected {
        None => true,
        Some(DomQueryRole::Link) => matches!(actual, SemanticRole::Link),
        Some(DomQueryRole::Button) => matches!(actual, SemanticRole::Button),
        Some(DomQueryRole::Field) => matches!(
            actual,
            SemanticRole::SearchField
                | SemanticRole::TextField
                | SemanticRole::Checkbox
                | SemanticRole::Radio
                | SemanticRole::Select
                | SemanticRole::Option
        ),
        Some(DomQueryRole::Heading) => matches!(actual, SemanticRole::Heading),
        Some(DomQueryRole::List) => matches!(actual, SemanticRole::List | SemanticRole::ListItem),
        Some(DomQueryRole::Table) => matches!(
            actual,
            SemanticRole::Table | SemanticRole::TableRow | SemanticRole::TableCell
        ),
        Some(DomQueryRole::Image) => matches!(actual, SemanticRole::Image),
        Some(DomQueryRole::Region) => matches!(actual, SemanticRole::Region),
    }
}

fn node_text_matches(node: &ArenaNode, needle: &str) -> bool {
    node.name
        .as_deref()
        .is_some_and(|name| contains_folded(name, needle))
        || node
            .text
            .iter()
            .any(|run| contains_folded(&run.text, needle))
}

#[cfg(test)]
mod tests;

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use bip_types::identity::SemanticNodeId;

use super::super::Reducer;
use crate::action::ActionProposal;
use crate::ids::IdSource;
use crate::time::Clock;
use crate::tool;

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// How a proposal is named in the repetition register.
    ///
    /// A proposal carries a tool, a tab, a node, and — for in-tab navigation —
    /// a destination address. For a browser proposal the target *is* the
    /// argument list, which is the `(tool, target)` half of the key decision
    /// 0054 section 5 counts. The address is an operand, not prose.
    pub(crate) fn call_of(proposal: &ActionProposal) -> tool::CallFingerprint {
        if proposal.tool_name() == tool::NAVIGATE_TOOL {
            tool::CallFingerprint::of_target(
                proposal.tool_name(),
                Some(proposal.tab_id().as_str()),
                proposal.destination_address(),
            )
        } else {
            tool::CallFingerprint::of_target(
                proposal.tool_name(),
                Some(proposal.tab_id().as_str()),
                proposal.node_id().map(SemanticNodeId::as_str),
            )
        }
    }

    /// Whether this proposal names a tool it may actually call.
    ///
    /// Four clauses, and the last two are the ones worth explaining. Both make
    /// the two class fields of [`crate::action::ActionProposal`] say what their
    /// own documentation claims — that the class is read from the tool
    /// definition and never from the proposal's author — and neither was
    /// checked until it was written here.
    ///
    /// The idempotency clause: `Action::recovery_rule` computes straight from
    /// the proposal's field, and `PureRead` is the one class whose rule permits
    /// an **unattended retry**, so an author that declared `PureRead` for a
    /// consequential tool was choosing to have its own failures retried with
    /// nobody deciding they should be.
    ///
    /// The action-class clause: [`crate::authority::ActionClass`] is the whole
    /// of what `policy-engine` is asked about, and its baseline risk differs by
    /// class — an observation is a local read and a synthetic click is a
    /// reversible disclosure. A click that declared itself an observation would
    /// be granted as a read, which is exactly the widening decision 0033's
    /// single grantor may never be talked into, and it would happen with every
    /// later check satisfied. So the class the table gives the row is the only
    /// class a proposal of that row may carry.
    ///
    /// A mismatch is refused rather than corrected. Substituting the table's
    /// value would leave the journal holding a proposal nobody made, and replay
    /// would reconstruct a decision that was never taken.
    ///
    /// What this deliberately does **not** decide: a row whose dispatch is not
    /// a browser action — [`crate::tool::ToolDispatch::Person`],
    /// [`crate::tool::ToolDispatch::Loop`] and
    /// [`crate::tool::ToolDispatch::Unserved`] — has no class in the table to
    /// compare against, and this guard leaves such a proposal exactly where it
    /// found it. `user.handover` can still be *proposed* that way
    /// (`tests/unconditional_handover.rs` reads the allowlist through that
    /// path), and proposing it grants nothing, because the row has no action
    /// class for policy to be asked about. What the agent loop does with the
    /// same row is now [`crate::command::Command::RequestHandover`], which
    /// revokes authority and waits rather than reaching a page.
    pub(super) fn tool_available(&self, proposal: &crate::action::ActionProposal) -> bool {
        if !proposal.intent().is_proposable() {
            return false;
        }
        let lookup = tool::resolve(proposal.tool_name(), self.task.snapshot.milestone);
        lookup.is_available()
            && lookup.entry().is_some_and(|entry| {
                self.allowlisted(entry)
                    && entry.idempotency == proposal.idempotency()
                    && entry
                        .dispatch
                        .action_class()
                        .is_none_or(|class| class == proposal.action_class())
            })
    }

    /// Whether this task may call `tool_name` at all.
    ///
    /// The milestone half and the allowlist half, exactly as
    /// [`Guard::ToolAvailable`] asks them, so the agent loop cannot propose
    /// something the guard would then refuse. The allowlist admits the
    /// canonical registry row rather than a spelling supplied by the model:
    /// a namespace row is therefore admitted whole or not at all, exactly as
    /// [`crate::tool::EffectiveToolSet`] presents it.
    pub fn admits_tool(&self, tool_name: &str) -> bool {
        let lookup = tool::resolve(tool_name, self.task.snapshot.milestone);
        lookup.is_available() && lookup.entry().is_some_and(|entry| self.allowlisted(entry))
    }

    fn allowlisted(&self, entry: &crate::tool::ToolEntry) -> bool {
        // An unconditional name is reachable whatever the allowlist says, and
        // this admits nothing: `tool::resolve` has already answered for the
        // milestone, and the caller ANDs that verdict with this one. What it
        // stops is a narrowed allowlist deleting the way out — see
        // [`tool::UNCONDITIONAL_TOOLS`] for why an escape removed is not a
        // narrowing.
        if tool::is_unconditional(entry.name) {
            return true;
        }
        let allowlist = &self.task.snapshot.tool_allowlist;
        allowlist.is_empty()
            || allowlist
                .iter()
                .any(|allowed| allowed == tool::allowlist_name(entry.name))
    }
}

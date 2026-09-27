// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Loop-local tools: search, activate, and one nested pass under this task.
//!
//! Settled on [`TurnResidency`], never as a journal command. A loop call
//! reaches no page, spends no capability, and does not take a page lease.
//! Nested work stays under the same task identity (decision 0009 point 4).

use super::reply::{
    ModelToolCall, TurnResidency, MAX_LOOP_RESULT_BYTES, MAX_LOOP_RESULT_PIECES,
    MAX_TURN_TOOL_CALLS,
};
use crate::ids::IdSource;
use crate::reducer::Reducer;
use crate::time::Clock;
use crate::tool::{
    self, ArgumentValue, EffectiveToolSet, Milestone, ToolDispatch, ToolEntry, ACTIVATE_TOOL,
    MAX_NESTED_DEPTH, SEARCH_TOOLS, SPAWN_RUN,
};

/// Search returns at most one turn's worth of exact names. The registry may
/// grow without letting one discovery call consume an unbounded prompt.
const MAX_SEARCH_RESULT_NAMES: usize = MAX_TURN_TOOL_CALLS;

/// Space held back for the fixed label and two decimal counts.
const SEARCH_RESULT_HEADER_BYTES: usize = 128;

/// What a loop-local call produced, as a compiled-in outcome.
///
/// No variant carries page or model text. A hit count is a number; an
/// activated name and a nested goal live beside this on the residency.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LoopOutcome {
    /// `tool.search` finished, with how many deferred rows matched.
    Searched {
        /// How many deferred-available rows the query matched.
        hits: u32,
    },
    /// `tool.activate` finished. `name_known` is whether the name was a
    /// deferred row this milestone already admits.
    Activated {
        /// Whether the exact registered name was a deferred-available row.
        name_known: bool,
    },
    /// `run.spawn` started a nested pass under this task.
    Spawned,
    /// The native table engine returned a complete bounded table.
    TableReshaped {
        /// Data rows in the output.
        rows: u32,
        /// Columns in the output.
        columns: u32,
    },
    /// The native table engine refused malformed input, a bound, or cancellation.
    TableRefused,
    /// The call was refused: empty goal, depth ceiling, or not a loop tool.
    Refused,
}

impl LoopOutcome {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Searched { .. } => "searched",
            Self::Activated { .. } => "activated",
            Self::Spawned => "spawned",
            Self::TableReshaped { .. } => "table_reshaped",
            Self::TableRefused => "table_refused",
            Self::Refused => "refused",
        }
    }

    /// The compiled-in sentence the model is shown for this outcome.
    ///
    /// Hit counts use a handful of static sentences rather than echoing a
    /// query: zero, one, or several. The exact count stays on [`Self::Searched`].
    pub const fn sentence(self) -> &'static str {
        match self {
            Self::Searched { hits: 0 } => "found no deferred tools",
            Self::Searched { hits: 1 } => "found 1 deferred tool",
            Self::Searched { .. } => "found several deferred tools",
            Self::Activated { name_known: true } => "loaded the named tool into this task",
            Self::Activated { name_known: false } => "no deferred tool has that name",
            Self::Spawned => "started a nested pass on this task",
            Self::TableReshaped { .. } => "reshaped the table in the portable core",
            Self::TableRefused => "the table reshape was refused",
            Self::Refused => "the nested pass was refused",
        }
    }
}

/// The pure settlement of one loop-local call and its transient result.
///
/// No browser action, no network, no capability. `tools` is the authoritative
/// milestone-and-allowlist-filtered set for this task, with prior activations
/// already applied. Search and activation only inspect that set, so neither
/// can reveal or introduce a name review excluded.
pub fn loop_tool_result(
    entry: &ToolEntry,
    call: &ModelToolCall,
    tools: &EffectiveToolSet,
) -> (LoopOutcome, Vec<String>) {
    if entry.dispatch != ToolDispatch::Loop {
        return (LoopOutcome::Refused, Vec::new());
    }
    match entry.name {
        SEARCH_TOOLS => search_result(call, tools),
        ACTIVATE_TOOL => activate_result(call, tools),
        SPAWN_RUN => (spawn_result(call), Vec::new()),
        _ => (LoopOutcome::Refused, Vec::new()),
    }
}

fn search_result(call: &ModelToolCall, tools: &EffectiveToolSet) -> (LoopOutcome, Vec<String>) {
    let query = text_argument(call, "query").unwrap_or("");
    let matching = tools.search_names(query);
    let hits = u32::try_from(matching.len()).unwrap_or(u32::MAX);
    let result = bounded_search_result(&matching);
    (LoopOutcome::Searched { hits }, result)
}

fn bounded_search_result(matching: &[&'static str]) -> Vec<String> {
    if matching.is_empty() {
        return Vec::new();
    }
    let body_limit = MAX_LOOP_RESULT_BYTES.saturating_sub(SEARCH_RESULT_HEADER_BYTES);
    let mut body = String::new();
    let mut shown = 0_usize;
    for name in matching.iter().take(MAX_SEARCH_RESULT_NAMES) {
        let separator = if body.is_empty() { "" } else { ", " };
        let Some(next_bytes) = body
            .len()
            .checked_add(separator.len())
            .and_then(|bytes| bytes.checked_add(name.len()))
        else {
            break;
        };
        if next_bytes > body_limit {
            break;
        }
        body.push_str(separator);
        body.push_str(name);
        shown = shown.saturating_add(1);
    }
    if shown == 0 {
        return Vec::new();
    }
    vec![format!(
        "matching deferred tools ({shown} of {}): {body}",
        matching.len()
    )]
}

fn activate_result(call: &ModelToolCall, tools: &EffectiveToolSet) -> (LoopOutcome, Vec<String>) {
    let Some(name) = text_argument(call, "name") else {
        return (LoopOutcome::Activated { name_known: false }, Vec::new());
    };
    let Some(canonical) = tools.activatable_name(name) else {
        return (LoopOutcome::Activated { name_known: false }, Vec::new());
    };
    (
        LoopOutcome::Activated { name_known: true },
        vec![format!("activated deferred tool: {canonical}")],
    )
}

fn spawn_result(call: &ModelToolCall) -> LoopOutcome {
    match text_argument(call, "goal").map(str::trim) {
        Some(goal) if !goal.is_empty() => LoopOutcome::Spawned,
        Some(_) | None => LoopOutcome::Refused,
    }
}

fn text_argument<'a>(call: &'a ModelToolCall, name: &str) -> Option<&'a str> {
    call.arguments.iter().find_map(|argument| {
        if argument.name != name {
            return None;
        }
        match argument.value {
            ArgumentValue::Text(ref value) => Some(value.as_str()),
            ArgumentValue::Handle(_)
            | ArgumentValue::Address(_)
            | ArgumentValue::Count(_)
            | ArgumentValue::Flag(_)
            | ArgumentValue::Choice(_)
            | ArgumentValue::SuppliedValue(_) => None,
        }
    })
}

pub(super) fn loop_entry_for(
    milestone: Milestone,
    residency: &TurnResidency,
    sequence: u32,
) -> Option<&'static ToolEntry> {
    let call = residency.call(sequence)?;
    let entry = tool::resolve(&call.tool_name, milestone).entry()?;
    (entry.dispatch == ToolDispatch::Loop).then_some(entry)
}

impl TurnResidency {
    /// Records `outcome` for the `sequence`-th call.
    ///
    /// Returns `false` when there is no such call, the call is not a loop
    /// tool, or that sequence is already settled. A spawn that would pass
    /// [`MAX_NESTED_DEPTH`] is stored as [`LoopOutcome::Refused`].
    pub fn settle_loop(&mut self, sequence: u32, outcome: LoopOutcome) -> bool {
        self.settle_loop_with_result(sequence, outcome, Vec::new())
    }

    /// Records one loop-local outcome and its bounded transient result pieces.
    ///
    /// The pieces are visible only to the next model turn. They are never
    /// journalled, which is the same residency rule as page text and saved
    /// Library search results.
    pub fn settle_loop_with_result(
        &mut self,
        sequence: u32,
        outcome: LoopOutcome,
        result: Vec<String>,
    ) -> bool {
        let Some(call) = self.call(sequence).cloned() else {
            return false;
        };
        if self.loop_outcomes.contains_key(&sequence)
            || result.len() > MAX_LOOP_RESULT_PIECES
            || result
                .iter()
                .try_fold(0_usize, |total, piece| total.checked_add(piece.len()))
                .is_none_or(|bytes| bytes > MAX_LOOP_RESULT_BYTES)
        {
            return false;
        }
        let is_loop = tool::REGISTRY
            .iter()
            .any(|entry| entry.name == call.tool_name && entry.dispatch == ToolDispatch::Loop);
        if !is_loop {
            return false;
        }
        let outcome = self.apply_loop_side_effects(&call, outcome);
        self.loop_outcomes.insert(sequence, outcome);
        if !result.is_empty() {
            self.loop_results.insert(sequence, result);
        }
        true
    }

    /// The outcome recorded for `sequence`, when one has been settled.
    pub fn loop_outcome(&self, sequence: u32) -> Option<&LoopOutcome> {
        self.loop_outcomes.get(&sequence)
    }

    /// The transient result pieces for one settled loop call.
    pub fn loop_result(&self, sequence: u32) -> Option<&[String]> {
        self.loop_results.get(&sequence).map(Vec::as_slice)
    }

    /// The first loop call that has not yet been settled.
    ///
    /// The reducer's [`Reducer::pending_loop_call`] is the walk that also
    /// honours attemptability and prior actions; this is the residency half.
    pub fn pending_loop_sequence(&self) -> Option<u32> {
        for (index, call) in self.reply().tool_calls.iter().enumerate() {
            let Ok(sequence) = u32::try_from(index) else {
                continue;
            };
            if self.loop_outcomes.contains_key(&sequence) {
                continue;
            }
            let is_loop = tool::REGISTRY
                .iter()
                .any(|entry| entry.name == call.tool_name && entry.dispatch == ToolDispatch::Loop);
            if is_loop {
                return Some(sequence);
            }
        }
        None
    }

    /// Exact registered names this turn has activated, in settlement order.
    pub fn activated_names(&self) -> &[&'static str] {
        &self.activated
    }

    /// The nested goal the last successful spawn carried, when one did.
    pub fn nested_goal(&self) -> Option<&str> {
        self.nested_goal.as_deref()
    }

    /// Current transient call depth, including a pass started by this reply.
    pub const fn nested_depth(&self) -> u32 {
        self.nested_depth
    }

    fn apply_loop_side_effects(
        &mut self,
        call: &ModelToolCall,
        outcome: LoopOutcome,
    ) -> LoopOutcome {
        match outcome {
            LoopOutcome::Activated { name_known: true } => {
                if let Some(name) = text_argument(call, "name") {
                    if let Some(canonical) = tool::REGISTRY
                        .iter()
                        .find_map(|entry| entry.callable_name(name))
                    {
                        if !self.activated.contains(&canonical) {
                            self.activated.push(canonical);
                        }
                    }
                }
                outcome
            }
            LoopOutcome::Spawned => {
                if self.nested_depth >= MAX_NESTED_DEPTH {
                    return LoopOutcome::Refused;
                }
                let Some(goal) = text_argument(call, "goal").map(str::trim) else {
                    return LoopOutcome::Refused;
                };
                if goal.is_empty() {
                    return LoopOutcome::Refused;
                }
                self.nested_goal = Some(goal.to_owned());
                self.nested_depth = self.nested_depth.saturating_add(1);
                LoopOutcome::Spawned
            }
            LoopOutcome::Searched { .. }
            | LoopOutcome::Activated { name_known: false }
            | LoopOutcome::TableReshaped { .. }
            | LoopOutcome::TableRefused
            | LoopOutcome::Refused => outcome,
        }
    }
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// The next attemptable loop call that still needs settlement.
    ///
    /// `None` when the walk is waiting on a browser action, has nothing left,
    /// or every loop call in this reply is already settled.
    pub fn pending_loop_call<'a>(
        &self,
        residency: &'a TurnResidency,
    ) -> Option<(u32, &'a ModelToolCall, &'static ToolEntry)> {
        let milestone = self.task().snapshot().milestone;
        let ordinal = self.model_turn()?.ordinal();
        for disposition in self.turn_dispositions(residency) {
            if !disposition.verdict.is_attemptable() {
                continue;
            }
            let entry = loop_entry_for(milestone, residency, disposition.sequence)?;
            if residency.loop_outcome(disposition.sequence).is_some() {
                continue;
            }
            if self.turn_action(ordinal, disposition.sequence).is_some() {
                return None;
            }
            let call = residency.call(disposition.sequence)?;
            return Some((disposition.sequence, call, entry));
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::super::reply::{ModelReply, TurnPage, TurnResidency};
    use super::super::turn::{ModelStopReason, RenderShape, TurnUsage};
    use super::{loop_tool_result, LoopOutcome};
    use crate::agent::ModelToolCall;
    use crate::handle::HandleTable;
    use crate::ids::ModelCallId;
    use crate::tool::{
        ArgumentValue, EffectiveToolSet, Milestone, SuppliedArgument, ToolDispatch, ACTIVATE_TOOL,
        SEARCH_TOOLS, SPAWN_RUN,
    };
    use bip_types::identity::TabId;

    fn call(name: &str, argument: &str, value: &str) -> ModelToolCall {
        ModelToolCall::new(
            name,
            vec![SuppliedArgument::new(
                argument,
                ArgumentValue::Text(value.to_owned()),
            )],
        )
    }

    fn entry(name: &str) -> &'static crate::tool::ToolEntry {
        crate::tool::REGISTRY
            .iter()
            .find(|entry| entry.name == name)
            .unwrap_or_else(|| unreachable!("{name} is registered"))
    }

    fn tools(milestone: Milestone) -> EffectiveToolSet {
        EffectiveToolSet::for_task(milestone, &[])
    }

    fn residency(calls: Vec<ModelToolCall>) -> TurnResidency {
        TurnResidency::read(
            ModelCallId::new("model-task-1"),
            TurnPage::new(
                TabId::new("tab_1"),
                HandleTable::new(),
                RenderShape::empty([0; 32]),
            ),
            ModelReply {
                stop: ModelStopReason::ToolCall,
                overflow: None,
                usage: TurnUsage::default(),
                answer_segments: 1,
                tool_calls: calls,
            },
        )
        .expect("bounded calls")
    }

    #[test]
    fn search_counts_deferred_hits_and_skips_fill_at_m3() {
        let search = entry(SEARCH_TOOLS);
        let document = call(SEARCH_TOOLS, "query", "document");
        let (outcome, result) = loop_tool_result(search, &document, &tools(Milestone::M4));
        assert_eq!(outcome, LoopOutcome::Searched { hits: 1 });
        assert_eq!(
            result,
            vec!["matching deferred tools (1 of 1): page.pdf.inspect"]
        );
        let fill = call(SEARCH_TOOLS, "query", "fill");
        let (outcome, result) = loop_tool_result(search, &fill, &tools(Milestone::M3));
        assert_eq!(outcome, LoopOutcome::Searched { hits: 0 });
        assert!(result.is_empty());
        assert_eq!(
            LoopOutcome::Searched { hits: 0 }.sentence(),
            "found no deferred tools"
        );
        assert_eq!(
            LoopOutcome::Searched { hits: 1 }.sentence(),
            "found 1 deferred tool"
        );
        assert_eq!(
            LoopOutcome::Searched { hits: 4 }.sentence(),
            "found several deferred tools"
        );
    }

    #[test]
    fn activate_knows_a_deferred_available_name_and_refuses_unknown_ones() {
        let activate = entry(ACTIVATE_TOOL);
        let pdf = call(ACTIVATE_TOOL, "name", "page.pdf.inspect");
        let (outcome, result) = loop_tool_result(activate, &pdf, &tools(Milestone::M4));
        assert_eq!(outcome, LoopOutcome::Activated { name_known: true });
        assert_eq!(result, vec!["activated deferred tool: page.pdf.inspect"]);
        assert_eq!(
            loop_tool_result(activate, &pdf, &tools(Milestone::M3)).0,
            LoopOutcome::Activated { name_known: false }
        );
        let fill = call(ACTIVATE_TOOL, "name", "browser.form.fill");
        assert_eq!(
            loop_tool_result(activate, &fill, &tools(Milestone::M3)).0,
            LoopOutcome::Activated { name_known: false }
        );
        let unknown = call(ACTIVATE_TOOL, "name", "not-a-tool");
        assert_eq!(
            loop_tool_result(activate, &unknown, &tools(Milestone::M8)).0,
            LoopOutcome::Activated { name_known: false }
        );
    }

    #[test]
    fn search_and_activation_cannot_cross_the_reviewed_allowlist() {
        let allowed = vec![
            SEARCH_TOOLS.to_owned(),
            ACTIVATE_TOOL.to_owned(),
            "page.pdf.inspect".to_owned(),
        ];
        let reviewed = EffectiveToolSet::for_task(Milestone::M6, &allowed);
        let search = call(SEARCH_TOOLS, "query", "image");
        let (outcome, result) = loop_tool_result(entry(SEARCH_TOOLS), &search, &reviewed);
        assert_eq!(outcome, LoopOutcome::Searched { hits: 0 });
        assert!(result.is_empty());

        for name in [
            "page.images.describe",
            "page.images.read_text",
            "page.images.caption",
        ] {
            let activate = call(ACTIVATE_TOOL, "name", name);
            let (outcome, result) = loop_tool_result(entry(ACTIVATE_TOOL), &activate, &reviewed);
            assert_eq!(
                outcome,
                LoopOutcome::Activated { name_known: false },
                "{name}"
            );
            assert!(result.is_empty(), "{name}");
        }
    }

    #[test]
    fn search_returns_bounded_exact_registry_names() {
        let reviewed = tools(Milestone::M8);
        let search = call(SEARCH_TOOLS, "query", "a");
        let (LoopOutcome::Searched { hits }, result) =
            loop_tool_result(entry(SEARCH_TOOLS), &search, &reviewed)
        else {
            unreachable!("tool.search has one searched outcome")
        };
        assert!(hits > 0);
        let [piece] = result.as_slice() else {
            unreachable!("a non-empty search has one result piece")
        };
        assert!(piece.len() <= super::MAX_LOOP_RESULT_BYTES);
        let (_, names) = piece
            .split_once(": ")
            .unwrap_or_else(|| unreachable!("the compiled result has a label"));
        let returned: Vec<&str> = names.split(", ").collect();
        assert!(returned.len() <= super::MAX_SEARCH_RESULT_NAMES);
        for name in returned {
            assert!(
                reviewed.search_names("a").contains(&name),
                "{name} was not a compiled-in matching name"
            );
            assert!(crate::tool::resolve(name, Milestone::M8).is_available());
        }
    }

    #[test]
    fn spawn_accepts_a_goal_and_refuses_whitespace() {
        let spawn = entry(SPAWN_RUN);
        let goal = call(SPAWN_RUN, "goal", "narrow the comparison to battery life");
        assert_eq!(
            loop_tool_result(spawn, &goal, &tools(Milestone::M3)).0,
            LoopOutcome::Spawned
        );
        let blank = call(SPAWN_RUN, "goal", "   ");
        assert_eq!(
            loop_tool_result(spawn, &blank, &tools(Milestone::M3)).0,
            LoopOutcome::Refused
        );
    }

    #[test]
    fn a_non_loop_entry_is_refused() {
        let navigate = entry(crate::tool::NAVIGATE_TOOL);
        let ignored = call(crate::tool::NAVIGATE_TOOL, "query", "ignored");
        assert_eq!(
            loop_tool_result(navigate, &ignored, &tools(Milestone::M3)).0,
            LoopOutcome::Refused
        );
        assert_eq!(
            navigate.dispatch,
            ToolDispatch::BrowserAction(crate::authority::ActionClass::OpenLink)
        );
    }

    #[test]
    fn a_second_nested_spawn_is_refused() {
        let calls = vec![
            call(SPAWN_RUN, "goal", "narrow to battery life"),
            call(SPAWN_RUN, "goal", "narrow further to two models"),
        ];
        let mut residency = residency(calls);

        let first = loop_tool_result(
            entry(SPAWN_RUN),
            residency.call(0).expect("first"),
            &tools(Milestone::M3),
        )
        .0;
        assert_eq!(first, LoopOutcome::Spawned);
        assert!(residency.settle_loop(0, first));
        assert_eq!(residency.nested_depth(), 1);
        assert_eq!(residency.nested_goal(), Some("narrow to battery life"));

        let second = loop_tool_result(
            entry(SPAWN_RUN),
            residency.call(1).expect("second"),
            &tools(Milestone::M3),
        )
        .0;
        assert_eq!(second, LoopOutcome::Spawned);
        assert!(residency.settle_loop(1, second));
        assert_eq!(residency.loop_outcome(1), Some(&LoopOutcome::Refused));
        assert_eq!(residency.nested_depth(), 1);
        assert_eq!(
            LoopOutcome::Refused.sentence(),
            "the nested pass was refused"
        );
    }

    #[test]
    fn a_nested_reply_cannot_spawn_again_across_model_turns() {
        let mut residency = residency(vec![call(
            SPAWN_RUN,
            "goal",
            "recurse from the nested reply",
        )])
        .with_nested_depth(crate::tool::MAX_NESTED_DEPTH)
        .expect("the compiled maximum is admissible");
        let proposed = loop_tool_result(
            entry(SPAWN_RUN),
            residency.call(0).expect("spawn call"),
            &tools(Milestone::M3),
        )
        .0;

        assert!(residency.settle_loop(0, proposed));
        assert_eq!(residency.loop_outcome(0), Some(&LoopOutcome::Refused));
        assert_eq!(residency.nested_depth(), crate::tool::MAX_NESTED_DEPTH);
        assert!(residency.nested_goal().is_none());
    }

    #[test]
    fn a_reply_cannot_claim_a_depth_past_the_compiled_ceiling() {
        assert!(residency(Vec::new())
            .with_nested_depth(crate::tool::MAX_NESTED_DEPTH.saturating_add(1))
            .is_none());
    }
}

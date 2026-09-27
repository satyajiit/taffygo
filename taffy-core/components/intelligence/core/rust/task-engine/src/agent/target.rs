// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed resolution of the browser-owned target one model call names.

use bip_types::identity::{NodeHandle, SemanticNodeId, TabId};

use super::reply::{ModelToolCall, NotAttempted, PersonsPages, TurnPage, TurnResidency};
use crate::ids::IdSource;
use crate::reducer::Reducer;
use crate::time::Clock;
use crate::tool::{IdempotencyClass, ToolDispatch, ToolEntry};

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// The tabs of the pages the person attached, for as long as this task
    /// has a tab of its own to act in instead (decision 0237).
    ///
    /// Read from the task's own durable record and from nothing a turn
    /// carries: the sources consent named when the task started, which
    /// `on_start_task` froze and nothing changes afterwards, and the tab the
    /// browser opened for the task. The live source list is the wrong one to
    /// read — it also holds every site the task went and found, and those
    /// stand in tabs the task owns. So is the projection's own tab, which is
    /// the current page with the lowest browser-issued source identity: a
    /// random UUID, so once the task's own tab is holding a page it names the
    /// person's page only by chance.
    ///
    /// The composer reads the same answer, so the page a turn is told it may
    /// only read and the page a call is refused on are one set.
    pub fn persons_pages(&self) -> PersonsPages {
        let snapshot = self.task().snapshot();
        PersonsPages::new(
            snapshot
                .consented_sources
                .iter()
                .map(|source| &source.tab_id),
            snapshot.discovery_tab_id.as_ref(),
        )
    }
}

/// What one call acts on after every opaque handle has resolved exactly.
pub(super) struct CallTarget {
    pub(super) tab_id: TabId,
    pub(super) node_id: Option<SemanticNodeId>,
    pub(super) node_handle: Option<NodeHandle>,
    pub(super) task_tab: Option<crate::action::TaskTabTarget>,
    pub(super) task_download: Option<(
        crate::BrowserSessionId,
        super::task_downloads::TaskDownloadSnapshot,
    )>,
}

/// What the call acts on: the node its number names, or the page it was read
/// from.
///
/// A tool that declares a handle parameter and supplies it must resolve it. A
/// number that resolves to nothing is [`NotAttempted::HandleUnknown`] and
/// never the nearest node; one that resolves to a node on a page the tab has
/// left is [`NotAttempted::NodeHandleFromAPageTheTabLeft`], which is a
/// different fact and gets different advice (decision 0208). One that
/// resolves to a node on `persons_pages` is admitted for a pure read and for
/// nothing else, [`NotAttempted::NodeOnThePersonsPage`] (decision 0237). A
/// media source is narrower still: it must name one
/// completed browser-custodied audio/video download within the fixed byte
/// ceiling, and the durable download GUID stays opaque to the model.
pub(super) fn call_target(
    residency: &TurnResidency,
    entry: &ToolEntry,
    call: &ModelToolCall,
    persons_pages: &PersonsPages,
) -> Result<CallTarget, NotAttempted> {
    let page = residency.page();
    // The person's page is read-only to the task, and every move happens in a
    // tab of Taffy's own. That is what the consent sheet says in two
    // sentences — "titles and text go directly to your AI provider" for the
    // page, and "Taffy will find the site and may open up to 8 sites" for the
    // going — and it is what the browser can actually honour: a source can be
    // issued only for a tab the task owns, so a move that lands a tab the
    // *person* owns on a new site can never be admitted, the ledger reads it
    // as the person carrying the tab away, and the task's consent is
    // destroyed for good. Measured twice on a phone: once through
    // `browser.search`, and once through `browser.navigate` after only the
    // search had been routed (decision 0224).
    //
    // The set is the calls that land a tab and name no node. A click, a
    // submit and a link-open take their tab from the node they name, so
    // routing cannot help them: the node decides the tab. That tab was
    // assumed to be the right one, and a phone disproved it — numbers are
    // task-global, the model named a link on the person's page long after it
    // had moved into its own tab, and the person's tab was the one that went.
    // Such a call is refused below rather than routed (decision 0237). A
    // zero-source errand needs none of this: its page residency already is
    // the tab it owns.
    //
    // A move is routed here whether or not the tab holds anything yet, which
    // is what separates this from the no-node rule further down: landing a
    // document in a blank tab is exactly what a move is for, while a read of
    // a blank tab has nothing to read (decision 0225).
    let moves_a_tab = matches!(
        entry.name,
        "browser.search"
            | "browser.navigate"
            | "browser.back"
            | "browser.forward"
            | "browser.reload"
            | "browser.stop_loading"
    );
    let own_tab = moves_a_tab.then(|| page.discovery_tab()).flatten();
    if let Some(own_tab) = own_tab {
        return Ok(CallTarget {
            tab_id: own_tab.clone(),
            node_id: None,
            node_handle: None,
            task_tab: None,
            task_download: None,
        });
    }
    // Every arm below reaches for this tab, and an intent that carries an
    // empty identifier is refused by the canonical encoding rather than by
    // policy — which ends the walk instead of the call. Refuse the call here,
    // where the model is told and the task can still finish.
    if page.tab().as_str().is_empty() {
        return Err(NotAttempted::PageUnknown);
    }
    if entry.name == "browser.download.cancel" {
        return task_download_target(residency, call, page.tab().clone());
    }
    if entry.dispatch == ToolDispatch::ToolJob(crate::tool::ToolRuntime::Media) {
        return media_source_target(residency, call, page.tab().clone());
    }
    if matches!(entry.name, "browser.tabs.activate" | "browser.tabs.close") {
        // `tab` is `Parameter::required` and typed `Handle`, and
        // `tool::validate` runs before this function, so neither arm below is
        // reachable: a missing argument and a wrong type are both already
        // `ArgumentsRejected`. They answer that rather than `HandleUnknown`
        // because `let`-`else` needs an arm and a number nobody supplied is
        // not a number this task does not know (decision 0208).
        let Some(argument) = call
            .arguments
            .iter()
            .find(|argument| argument.name == "tab")
        else {
            return Err(NotAttempted::ArgumentsRejected);
        };
        let crate::tool::ArgumentValue::Handle(value) = argument.value else {
            return Err(NotAttempted::ArgumentsRejected);
        };
        let target = residency
            .task_tabs()
            .resolve(value)
            .cloned()
            .ok_or(NotAttempted::HandleUnknown)?;
        return Ok(CallTarget {
            tab_id: page.tab().clone(),
            node_id: None,
            node_handle: None,
            task_tab: Some(target),
            task_download: None,
        });
    }
    let Some(argument) = handle_argument(entry, call) else {
        // No node named, so this acts on "the page" — and once the task has
        // landed a document in a tab of its own, that is the page it is
        // working on. `page.tab()` is the first source in order, which for an
        // errand asked from a page was the person's page turn after turn: the
        // read that follows a search read the person's page again,
        // and the results the search had just landed were never read at all
        // (decision 0225). A call that does name a node still resolves that
        // node's own tab below, so the person's page stays reachable by
        // naming something on it — for a read, and once the task has a tab
        // of its own, for nothing else (decision 0237).
        let tab_id = page
            .own_tab_holding_a_page()
            .unwrap_or_else(|| page.tab())
            .clone();
        return Ok(CallTarget {
            tab_id,
            node_id: None,
            node_handle: None,
            task_tab: None,
            task_download: None,
        });
    };
    // Unreachable for the same reason: the parameter is typed `Handle` and
    // validation has already run (decision 0208).
    let crate::tool::ArgumentValue::Handle(value) = argument.value else {
        return Err(NotAttempted::ArgumentsRejected);
    };
    node_target(page, entry, value, persons_pages)
}

/// The argument a call names its node by: the first handle parameter the
/// tool declares that the call supplies.
fn handle_argument<'a>(
    entry: &ToolEntry,
    call: &'a ModelToolCall,
) -> Option<&'a crate::tool::SuppliedArgument> {
    entry
        .parameters
        .iter()
        .filter(|parameter| parameter.value_type == crate::tool::ParameterType::Handle)
        .find_map(|parameter| {
            call.arguments
                .iter()
                .find(|argument| argument.name == parameter.name)
        })
}

/// The number a call names its node by, when it names one.
///
/// The same argument [`call_target`] resolves, read by a caller that needs
/// the number itself rather than the node it names — a request for values
/// asks the table what else on that line's page only the person can supply
/// (decision 0238).
pub(super) fn named_number(entry: &ToolEntry, call: &ModelToolCall) -> Option<u32> {
    match handle_argument(entry, call)?.value {
        crate::tool::ArgumentValue::Handle(value) => Some(value),
        _ => None,
    }
}

/// The node `value` names on `page`, unless `entry` may not act on it there.
///
/// Every refusal below answers a number that resolved, so each says what is
/// wrong with acting on *that* node — which page it came from, whose page it
/// is, what kind of line it is — and not that the number is unknown.
fn node_target(
    page: &TurnPage,
    entry: &ToolEntry,
    value: u32,
    persons_pages: &PersonsPages,
) -> Result<CallTarget, NotAttempted> {
    let node = page
        .handles()
        .resolve_value(value)
        .ok_or(NotAttempted::HandleUnknown)?;
    if page.names_a_page_its_tab_left(node) {
        return Err(NotAttempted::NodeHandleFromAPageTheTabLeft);
    }
    // The person's page is read-only to a task that has a tab of its own, and
    // "read" is the registry's own word for it rather than a list kept here:
    // `PureRead` is the one class whose rule permits an unattended retry,
    // because a call of it changes nothing (decision 0054 section 4). A query
    // within a node, a form inspection and a video inspection keep reaching
    // the person's page by naming something on it (decision 0225). A scroll
    // and a request for values are not reads, and they are refused with the
    // rest: a scroll moves the person's own view of their page, and a request
    // for values is how filling the person's page would begin (decision 0237).
    // Checked before the two field tests below, because on this page no
    // field is the right one.
    if entry.idempotency != IdempotencyClass::PureRead && persons_pages.holds(&node.tab_id) {
        return Err(NotAttempted::NodeOnThePersonsPage);
    }
    // The browser draws a sheet only for a form or a field a person can type
    // into, and gives up on anything else without drawing one. Refused here,
    // the model is told why; given up on there, it learned nothing and asked
    // about the same heading again (decision 0192).
    if entry.name == "user.request_values" && !page.handles().takes_values(value) {
        return Err(NotAttempted::NotAField);
    }
    // The person's values are held for the exact fields the sheet showed, and
    // a fill aimed at any other line ends that approval for all of them. A
    // page prints a field twice, once with its label and no action and once
    // as the field that sets text, so the wrong one is easy to pick
    // (decision 0195).
    if entry.name == "browser.form.fill" && !page.handles().sets_text(value) {
        return Err(NotAttempted::NotATextField);
    }
    // The browser opens only a link whose address it read, and answers any
    // other line as a node that is gone. On the myAadhaar home page a model
    // named a control that leads nowhere twice, was told twice to read again,
    // and the errand ended with nothing (decision 0241).
    if entry.name == "browser.link.open" && !page.handles().opens_as_link(value) {
        return Err(NotAttempted::NotALink);
    }
    Ok(CallTarget {
        tab_id: node.tab_id.clone(),
        node_id: Some(node.node_id.clone()),
        node_handle: Some(node.clone()),
        task_tab: None,
        task_download: None,
    })
}

/// One completed browser-custodied audio or video download inside the fixed
/// byte ceiling, named by a handle. The durable GUID stays opaque to the model.
fn media_source_target(
    residency: &TurnResidency,
    call: &ModelToolCall,
    tab_id: bip_types::identity::TabId,
) -> Result<CallTarget, NotAttempted> {
    let Some(argument) = call
        .arguments
        .iter()
        .find(|argument| argument.name == "source")
    else {
        return Err(NotAttempted::ArgumentsRejected);
    };
    let crate::tool::ArgumentValue::Handle(value) = argument.value else {
        return Err(NotAttempted::ArgumentsRejected);
    };
    let snapshot = residency
        .task_downloads()
        .resolve(value)
        .filter(|snapshot| {
            snapshot.state() == super::task_downloads::TaskDownloadState::Complete
                && matches!(
                    snapshot.media_type(),
                    super::task_downloads::TaskDownloadMediaType::Audio
                        | super::task_downloads::TaskDownloadMediaType::Video
                )
                && (1..=16 * 1024 * 1024).contains(&snapshot.received_bytes())
        })
        .cloned()
        .ok_or(NotAttempted::HandleUnknown)?;
    let browser_session_id = residency
        .task_downloads()
        .browser_session_id()
        .cloned()
        .ok_or(NotAttempted::HandleUnknown)?;
    Ok(CallTarget {
        tab_id,
        node_id: None,
        node_handle: None,
        task_tab: None,
        task_download: Some((browser_session_id, snapshot)),
    })
}

fn task_download_target(
    residency: &TurnResidency,
    call: &ModelToolCall,
    tab_id: TabId,
) -> Result<CallTarget, NotAttempted> {
    let Some(argument) = call
        .arguments
        .iter()
        .find(|argument| argument.name == "download")
    else {
        return Err(NotAttempted::ArgumentsRejected);
    };
    let crate::tool::ArgumentValue::Handle(value) = argument.value else {
        return Err(NotAttempted::ArgumentsRejected);
    };
    let snapshot = residency
        .task_downloads()
        .resolve_task_started(value)
        .filter(|snapshot| snapshot.state() != super::task_downloads::TaskDownloadState::Complete)
        .cloned()
        .ok_or(NotAttempted::HandleUnknown)?;
    let browser_session_id = residency
        .task_downloads()
        .browser_session_id()
        .cloned()
        .ok_or(NotAttempted::HandleUnknown)?;
    Ok(CallTarget {
        tab_id,
        node_id: None,
        node_handle: None,
        task_tab: None,
        task_download: Some((browser_session_id, snapshot)),
    })
}

#[cfg(test)]
mod tests {
    use bip_types::identity::{
        FrameId, GraphRevision, NodeHandle, Origin, OriginKind, PageEpoch, SemanticNodeId, TabId,
    };

    use super::super::reply::{ModelReply, ModelToolCall, NotAttempted, TurnPage, TurnResidency};
    use super::super::turn::{ModelStopReason, RenderShape, TurnUsage};
    use super::{call_target, PersonsPages};
    use crate::handle::{HandleTable, ValueTarget};
    use crate::ids::ModelCallId;
    use crate::tool::{
        ArgumentValue, IdempotencyClass, ParameterType, SuppliedArgument, ToolDispatch, ToolEntry,
        ToolRuntime, REGISTRY,
    };

    const PERSONS_TAB: &str = "tab_1";
    const OWN_TAB: &str = "discovery-tab-1";
    /// A second tab the task opened for itself, beside the prepared one.
    const OPENED_TAB: &str = "tab_9";

    fn persons_page_beside_its_own_tab() -> PersonsPages {
        PersonsPages::new([&TabId::new(PERSONS_TAB)], Some(&TabId::new(OWN_TAB)))
    }

    /// Every registered name that `call_target` resolves through the page's
    /// number table, with the handle parameter it names the node by. The
    /// three arms above that branch resolve their number through a table of
    /// their own — task tabs, task downloads, media sources — and name no
    /// node on any page.
    fn names_a_page_node() -> Vec<(&'static ToolEntry, &'static str, &'static str)> {
        REGISTRY
            .iter()
            .filter(|entry| {
                !matches!(
                    entry.name,
                    "browser.tabs.activate" | "browser.tabs.close" | "browser.download.cancel"
                ) && entry.dispatch != ToolDispatch::ToolJob(ToolRuntime::Media)
            })
            .filter_map(|entry| {
                let parameter = entry
                    .parameters
                    .iter()
                    .find(|parameter| parameter.value_type == ParameterType::Handle)?;
                Some((entry, parameter.name))
            })
            .flat_map(|(entry, parameter)| {
                entry
                    .callable_names()
                    .map(move |name| (entry, name, parameter))
            })
            .collect()
    }

    /// A turn that printed one number, for a field standing in `tab`, and the
    /// call of `name` that names it. A field, so that the two field tests
    /// after the new one cannot be what refuses a call here.
    fn naming_a_node_on(tab: &str, name: &str, parameter: &str) -> (TurnResidency, ModelToolCall) {
        let mut handles = HandleTable::new();
        let node = NodeHandle::new(
            TabId::new(tab),
            FrameId::new("frame_1"),
            PageEpoch::new("epoch_1"),
            GraphRevision(4),
            SemanticNodeId::new("n-1"),
            Origin {
                kind: OriginKind::Tuple,
                serialization: Some("https://example.test".to_owned()),
                opaque_id: None,
            },
        );
        let handle = handles
            .issue_marked(node, ValueTarget::Field)
            .expect("an empty table issues one number");
        let call = ModelToolCall::new(
            name,
            vec![SuppliedArgument::new(
                parameter,
                ArgumentValue::Handle(handle.value()),
            )],
        );
        let page = TurnPage::new(
            TabId::new(PERSONS_TAB),
            handles,
            RenderShape::empty([0; 32]),
        )
        .with_discovery_tab(Some(TabId::new(OWN_TAB)));
        let residency = TurnResidency::read(
            ModelCallId::new("model-task-1"),
            page,
            ModelReply {
                stop: ModelStopReason::ToolCall,
                overflow: None,
                usage: TurnUsage::default(),
                answer_segments: 1,
                tool_calls: vec![call.clone()],
            },
        )
        .expect("one call is within bounds");
        (residency, call)
    }

    /// The registry's own class sorts every tool that names a node on a page,
    /// with no list of names kept here: a pure read reaches the person's page,
    /// and nothing else does (decision 0237).
    #[test]
    fn only_a_pure_read_reaches_the_persons_page_once_the_task_has_its_own_tab() {
        let persons = persons_page_beside_its_own_tab();
        let (mut reads, mut refused) = (Vec::new(), Vec::new());
        for (entry, name, parameter) in names_a_page_node() {
            let (residency, call) = naming_a_node_on(PERSONS_TAB, name, parameter);
            let target = call_target(&residency, entry, &call, &persons);
            if entry.idempotency == IdempotencyClass::PureRead {
                let target = target.unwrap_or_else(|refusal| {
                    panic!("{name} reads, and was refused the person's page: {refusal:?}")
                });
                assert_eq!(target.tab_id, TabId::new(PERSONS_TAB), "{name}");
                reads.push(name);
            } else {
                assert_eq!(
                    target.err(),
                    Some(NotAttempted::NodeOnThePersonsPage),
                    "{name} is not a read and reached the person's page"
                );
                refused.push(name);
            }
        }
        // The two the decision argues by name, and the read beside them.
        assert!(refused.contains(&"browser.dom.scroll"), "{refused:?}");
        assert!(refused.contains(&"user.request_values"), "{refused:?}");
        assert!(refused.contains(&"browser.link.open"), "{refused:?}");
        assert!(reads.contains(&"browser.dom.query"), "{reads:?}");
        // The sweep found both halves; a filter that matched nothing would
        // pass every assertion above.
        assert!(
            reads.len() >= 3 && refused.len() >= 8,
            "{reads:?} {refused:?}"
        );
    }

    /// Every tab the task owns is still a tab it acts in: the one the browser
    /// prepared for it, and one it opened for itself.
    #[test]
    fn a_node_in_a_tab_the_task_owns_still_resolves_to_that_tab() {
        let persons = persons_page_beside_its_own_tab();
        for tab in [OWN_TAB, OPENED_TAB] {
            for (entry, name, parameter) in names_a_page_node() {
                let (residency, call) = naming_a_node_on(tab, name, parameter);
                let target = call_target(&residency, entry, &call, &persons)
                    .unwrap_or_else(|refusal| panic!("{name} in {tab}: {refusal:?}"));
                assert_eq!(target.tab_id, TabId::new(tab), "{name}");
            }
        }
    }

    /// With nothing set apart — a zero-source errand, or a task with no tab
    /// of its own — every call resolves exactly as it did before.
    #[test]
    fn an_empty_set_refuses_nothing() {
        for persons in [
            PersonsPages::default(),
            PersonsPages::new([], Some(&TabId::new(OWN_TAB))),
            PersonsPages::new([&TabId::new(PERSONS_TAB)], None),
        ] {
            for (entry, name, parameter) in names_a_page_node() {
                let (residency, call) = naming_a_node_on(PERSONS_TAB, name, parameter);
                let target = call_target(&residency, entry, &call, &persons)
                    .unwrap_or_else(|refusal| panic!("{name}: {refusal:?}"));
                assert_eq!(target.tab_id, TabId::new(PERSONS_TAB), "{name}");
            }
        }
    }

    /// The task's own tab is never the person's page, even named among them.
    #[test]
    fn the_tasks_own_tab_is_never_set_apart() {
        let own = TabId::new(OWN_TAB);
        let persons = PersonsPages::new([&TabId::new(PERSONS_TAB), &own], Some(&own));
        assert!(persons.holds(&TabId::new(PERSONS_TAB)));
        assert!(!persons.holds(&own));
        assert!(!persons.holds(&TabId::new(OPENED_TAB)));
    }
}

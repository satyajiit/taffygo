// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Generation-resident handles for assistant-owned task tabs.
//!
//! The browser owns the real tab identities and proves every snapshot. The
//! model sees only the short numbers minted here. This table lives beside one
//! task's loop residency and is never journalled, so a browser or core-service
//! restart invalidates every number instead of letting it name a tab in a new
//! session.

use std::collections::{BTreeMap, BTreeSet};

use crate::action::TaskTabTarget;
use crate::BrowserSessionId;

/// Most assistant-owned tabs one list result can expose to a model.
pub const MAX_TASK_TAB_RESULTS: usize = 16;

/// One member of a bounded, browser-verified list.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskTabSnapshot {
    target: TaskTabTarget,
    active: bool,
}

impl TaskTabSnapshot {
    pub const fn new(target: TaskTabTarget, active: bool) -> Self {
        Self { target, active }
    }

    pub const fn target(&self) -> &TaskTabTarget {
        &self.target
    }

    pub const fn is_active(&self) -> bool {
        self.active
    }
}

/// A native task-tab operation whose postcondition the browser verified.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TaskTabActionResult {
    Listed {
        browser_session_id: BrowserSessionId,
        tabs: Vec<TaskTabSnapshot>,
    },
    Activated {
        target: TaskTabTarget,
        was_already_active: bool,
    },
    Closed {
        target: TaskTabTarget,
        was_already_closed: bool,
    },
}

impl TaskTabActionResult {
    /// Accepts one list only when it is bounded, session-consistent, unique,
    /// and has at most one active member.
    pub fn listed(
        browser_session_id: BrowserSessionId,
        tabs: Vec<TaskTabSnapshot>,
    ) -> Result<Self, TaskTabResultError> {
        if tabs.len() > MAX_TASK_TAB_RESULTS {
            return Err(TaskTabResultError::TooManyTabs);
        }
        let mut tab_ids = BTreeSet::new();
        let mut active_count = 0usize;
        for tab in &tabs {
            if tab.target.browser_session_id() != &browser_session_id {
                return Err(TaskTabResultError::WrongBrowserSession);
            }
            if !tab_ids.insert(tab.target.tab_id().clone()) {
                return Err(TaskTabResultError::DuplicateTab);
            }
            active_count = active_count.saturating_add(usize::from(tab.active));
        }
        if active_count > 1 {
            return Err(TaskTabResultError::SeveralActiveTabs);
        }
        Ok(Self::Listed {
            browser_session_id,
            tabs,
        })
    }

    pub const fn activated(target: TaskTabTarget, was_already_active: bool) -> Self {
        Self::Activated {
            target,
            was_already_active,
        }
    }

    pub const fn closed(target: TaskTabTarget, was_already_closed: bool) -> Self {
        Self::Closed {
            target,
            was_already_closed,
        }
    }

    pub const fn browser_session_id(&self) -> &BrowserSessionId {
        match self {
            Self::Listed {
                browser_session_id, ..
            } => browser_session_id,
            Self::Activated { target, .. } | Self::Closed { target, .. } => {
                target.browser_session_id()
            }
        }
    }

    pub fn listed_tabs(&self) -> Option<&[TaskTabSnapshot]> {
        match self {
            Self::Listed { tabs, .. } => Some(tabs),
            Self::Activated { .. } | Self::Closed { .. } => None,
        }
    }

    pub const fn target(&self) -> Option<&TaskTabTarget> {
        match self {
            Self::Listed { .. } => None,
            Self::Activated { target, .. } | Self::Closed { target, .. } => Some(target),
        }
    }

    pub const fn tool_name(&self) -> &'static str {
        match self {
            Self::Listed { .. } => "browser.tabs.list",
            Self::Activated { .. } => "browser.tabs.activate",
            Self::Closed { .. } => "browser.tabs.close",
        }
    }
}

/// Why an asserted task-tab result cannot become model context.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskTabResultError {
    TooManyTabs,
    WrongBrowserSession,
    DuplicateTab,
    SeveralActiveTabs,
    TargetUnknown,
}

/// The task-local table that keeps browser identities out of model context.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TaskTabHandleTable {
    browser_session_id: Option<BrowserSessionId>,
    bindings: BTreeMap<u32, TaskTabSnapshot>,
}

impl TaskTabHandleTable {
    /// Resolves only a number issued by the most recent successful list.
    pub fn resolve(&self, handle: u32) -> Option<&TaskTabTarget> {
        self.bindings.get(&handle).map(TaskTabSnapshot::target)
    }

    pub const fn browser_session_id(&self) -> Option<&BrowserSessionId> {
        self.browser_session_id.as_ref()
    }

    pub fn is_empty(&self) -> bool {
        self.bindings.is_empty()
    }

    /// Applies a browser-verified result and returns the bounded words the
    /// current transcript may expose. A new list replaces every old handle.
    pub fn apply_verified(
        &mut self,
        result: &TaskTabActionResult,
    ) -> Result<TaskTabTranscriptOutcome, TaskTabResultError> {
        match result {
            TaskTabActionResult::Listed {
                browser_session_id,
                tabs,
            } => {
                let checked =
                    TaskTabActionResult::listed(browser_session_id.clone(), tabs.clone())?;
                let TaskTabActionResult::Listed { tabs, .. } = checked else {
                    return Err(TaskTabResultError::TargetUnknown);
                };
                self.browser_session_id = Some(browser_session_id.clone());
                self.bindings.clear();
                let mut transcript = Vec::with_capacity(tabs.len());
                for (index, tab) in tabs.into_iter().enumerate() {
                    let handle = u32::try_from(index)
                        .ok()
                        .and_then(|value| value.checked_add(1))
                        .ok_or(TaskTabResultError::TooManyTabs)?;
                    transcript.push(TaskTabTranscriptEntry {
                        handle,
                        active: tab.active,
                    });
                    self.bindings.insert(handle, tab);
                }
                Ok(TaskTabTranscriptOutcome::Listed(transcript))
            }
            TaskTabActionResult::Activated {
                target,
                was_already_active,
            } => {
                self.require_session(target.browser_session_id())?;
                let handle = self.handle_for(target)?;
                for tab in self.bindings.values_mut() {
                    tab.active = tab.target == *target;
                }
                Ok(TaskTabTranscriptOutcome::Activated {
                    handle,
                    was_already_active: *was_already_active,
                })
            }
            TaskTabActionResult::Closed {
                target,
                was_already_closed,
            } => {
                self.require_session(target.browser_session_id())?;
                let handle = self.handle_for(target)?;
                self.bindings.remove(&handle);
                Ok(TaskTabTranscriptOutcome::Closed {
                    handle,
                    was_already_closed: *was_already_closed,
                })
            }
        }
    }

    fn require_session(&self, session: &BrowserSessionId) -> Result<(), TaskTabResultError> {
        if self.browser_session_id.as_ref() == Some(session) {
            Ok(())
        } else {
            Err(TaskTabResultError::WrongBrowserSession)
        }
    }

    fn handle_for(&self, target: &TaskTabTarget) -> Result<u32, TaskTabResultError> {
        self.bindings
            .iter()
            .find_map(|(handle, binding)| (binding.target == *target).then_some(*handle))
            .ok_or(TaskTabResultError::TargetUnknown)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TaskTabTranscriptEntry {
    pub handle: u32,
    pub active: bool,
}

/// Content-safe task-tab outcome retained only with the reply that caused it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TaskTabTranscriptOutcome {
    Listed(Vec<TaskTabTranscriptEntry>),
    Activated {
        handle: u32,
        was_already_active: bool,
    },
    Closed {
        handle: u32,
        was_already_closed: bool,
    },
}

impl TaskTabTranscriptOutcome {
    pub const fn tool_name(&self) -> &'static str {
        match self {
            Self::Listed(_) => "browser.tabs.list",
            Self::Activated { .. } => "browser.tabs.activate",
            Self::Closed { .. } => "browser.tabs.close",
        }
    }

    /// Compiled-in pieces for the model transcript. The only varying values
    /// are one-through-sixteen handles and closed booleans; no browser identity
    /// or page-derived text can enter this result.
    pub fn result_pieces(&self) -> Vec<&'static str> {
        match self {
            Self::Listed(tabs) if tabs.is_empty() => {
                vec!["No assistant-owned task tabs are open."]
            }
            Self::Listed(tabs) => {
                let mut pieces = vec!["Assistant-owned task tabs: "];
                for (index, tab) in tabs.iter().enumerate() {
                    if index > 0 {
                        pieces.push(", ");
                    }
                    pieces.push("tab ");
                    pieces.push(handle_word(tab.handle));
                    pieces.push(if tab.active {
                        " (active)"
                    } else {
                        " (not active)"
                    });
                }
                pieces.push(".");
                pieces
            }
            Self::Activated {
                handle,
                was_already_active,
            } => vec![
                "Assistant-owned tab ",
                handle_word(*handle),
                if *was_already_active {
                    " was already active; the browser verified it is still active."
                } else {
                    " is active; the browser verified the selection."
                },
            ],
            Self::Closed {
                handle,
                was_already_closed,
            } => vec![
                "Assistant-owned tab ",
                handle_word(*handle),
                if *was_already_closed {
                    " was already closed; the browser verified it remains absent."
                } else {
                    " is closed; the browser verified it is absent."
                },
            ],
        }
    }
}

fn handle_word(handle: u32) -> &'static str {
    match handle {
        1 => "1",
        2 => "2",
        3 => "3",
        4 => "4",
        5 => "5",
        6 => "6",
        7 => "7",
        8 => "8",
        9 => "9",
        10 => "10",
        11 => "11",
        12 => "12",
        13 => "13",
        14 => "14",
        15 => "15",
        16 => "16",
        _ => "unknown",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bip_types::identity::{FrameId, GraphRevision, PageEpoch, TabId};

    fn session(value: &str) -> BrowserSessionId {
        BrowserSessionId::new(value).unwrap_or_else(|_| unreachable!())
    }

    fn target(session_id: &str, tab_id: &str, revision: u64) -> TaskTabTarget {
        TaskTabTarget::new(
            session(session_id),
            TabId::new(tab_id),
            FrameId::new(format!("frame-{tab_id}")),
            PageEpoch::new(format!("epoch-{tab_id}")),
            GraphRevision(revision),
        )
    }

    #[test]
    fn a_list_replaces_handles_and_never_accepts_ambiguous_members() {
        let current = session("browser-session-1");
        let first = TaskTabSnapshot::new(target(current.as_str(), "tab-a", 7), false);
        let second = TaskTabSnapshot::new(target(current.as_str(), "tab-b", 9), true);
        let mut table = TaskTabHandleTable::default();
        let result =
            TaskTabActionResult::listed(current.clone(), vec![first.clone(), second.clone()])
                .unwrap_or_else(|_| unreachable!());
        assert_eq!(
            table.apply_verified(&result),
            Ok(TaskTabTranscriptOutcome::Listed(vec![
                TaskTabTranscriptEntry {
                    handle: 1,
                    active: false,
                },
                TaskTabTranscriptEntry {
                    handle: 2,
                    active: true,
                },
            ]))
        );
        assert_eq!(table.resolve(1), Some(first.target()));
        assert_eq!(table.resolve(2), Some(second.target()));

        let replacement = TaskTabActionResult::listed(
            current.clone(),
            vec![TaskTabSnapshot::new(
                target(current.as_str(), "tab-c", 11),
                false,
            )],
        )
        .unwrap_or_else(|_| unreachable!());
        assert!(table.apply_verified(&replacement).is_ok());
        assert!(table.resolve(2).is_none());

        assert_eq!(
            TaskTabActionResult::listed(
                current.clone(),
                vec![TaskTabSnapshot::new(
                    target("another-session", "tab-d", 12),
                    false,
                )],
            ),
            Err(TaskTabResultError::WrongBrowserSession)
        );
        assert_eq!(
            TaskTabActionResult::listed(current.clone(), vec![first.clone(), first]),
            Err(TaskTabResultError::DuplicateTab)
        );
        assert_eq!(
            TaskTabActionResult::listed(
                current,
                vec![
                    TaskTabSnapshot::new(target("browser-session-1", "tab-e", 1), true),
                    TaskTabSnapshot::new(target("browser-session-1", "tab-f", 1), true),
                ],
            ),
            Err(TaskTabResultError::SeveralActiveTabs)
        );
    }

    #[test]
    fn activate_and_close_require_the_exact_issued_document() {
        let current = session("browser-session-1");
        let first = target(current.as_str(), "tab-a", 7);
        let second = target(current.as_str(), "tab-b", 9);
        let listed = TaskTabActionResult::listed(
            current,
            vec![
                TaskTabSnapshot::new(first.clone(), false),
                TaskTabSnapshot::new(second.clone(), true),
            ],
        )
        .unwrap_or_else(|_| unreachable!());
        let mut table = TaskTabHandleTable::default();
        assert!(table.apply_verified(&listed).is_ok());

        assert_eq!(
            table.apply_verified(&TaskTabActionResult::activated(first.clone(), false)),
            Ok(TaskTabTranscriptOutcome::Activated {
                handle: 1,
                was_already_active: false,
            })
        );
        assert!(table.bindings.get(&1).is_some_and(|tab| tab.active));
        assert!(table.bindings.get(&2).is_some_and(|tab| !tab.active));

        assert_eq!(
            table.apply_verified(&TaskTabActionResult::activated(
                target("browser-session-1", "tab-a", 8),
                false,
            )),
            Err(TaskTabResultError::TargetUnknown)
        );
        assert_eq!(
            table.apply_verified(&TaskTabActionResult::closed(
                target("another-session", "tab-a", 7),
                false,
            )),
            Err(TaskTabResultError::WrongBrowserSession)
        );
        assert_eq!(
            table.apply_verified(&TaskTabActionResult::closed(first, false)),
            Ok(TaskTabTranscriptOutcome::Closed {
                handle: 1,
                was_already_closed: false,
            })
        );
        assert!(table.resolve(1).is_none());
        assert_eq!(table.resolve(2), Some(&second));
    }

    #[test]
    fn transcript_words_never_expose_browser_identity() {
        let outcome = TaskTabTranscriptOutcome::Listed(vec![TaskTabTranscriptEntry {
            handle: 16,
            active: true,
        }]);
        let rendered = outcome.result_pieces().concat();
        assert_eq!(rendered, "Assistant-owned task tabs: tab 16 (active).");
        assert!(!rendered.contains("session"));
        assert!(!rendered.contains("frame"));
        assert!(!rendered.contains("epoch"));
    }
}

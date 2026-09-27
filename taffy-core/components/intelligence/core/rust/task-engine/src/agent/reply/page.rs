// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The projection one turn was answering: the page it was rendered from,
//! the numbers it issued, and the documents its tabs held at the time.

use bip_types::identity::{FrameId, NodeHandle, PageEpoch, TabId};

use crate::agent::turn::RenderShape;
use crate::handle::HandleTable;

/// The document one tab's root frame held when a turn's projection was taken.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CurrentDocument {
    /// The tab the observation came from.
    pub tab: TabId,
    /// That tab's root frame.
    pub frame: FrameId,
    /// The document lifetime the root frame held.
    pub page_epoch: PageEpoch,
}

/// The tabs holding the pages a person attached when they asked, for a task
/// that has a tab of its own to act in instead (decision 0237).
///
/// A call may read a tab in this set and may do nothing else there: the
/// person's page is read-only to the task, and every move belongs in the tab
/// the browser opened for it (decision 0224). The set is empty whenever there
/// is no such tab of the task's own — a research task, an errand whose consent
/// granted no discovery, one restored without its tab — because such a task
/// has nowhere else to act, and it is empty for a zero-source errand, which
/// never held a page of the person's at all. So an empty set is exactly the
/// behaviour before this type existed, and a set can only take something away
/// from what a call may do.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct PersonsPages {
    tabs: Vec<TabId>,
}

impl PersonsPages {
    /// The tabs of `attached` other than `own_tab`, or none at all when the
    /// task has no tab of its own.
    ///
    /// `attached` is the set the person consented to when the task started,
    /// not the set the task holds now: a site the task went and found is a
    /// source too, and it stands in a tab the task owns.
    pub fn new<'a>(attached: impl IntoIterator<Item = &'a TabId>, own_tab: Option<&TabId>) -> Self {
        let Some(own_tab) = own_tab else {
            return Self::default();
        };
        Self {
            tabs: attached
                .into_iter()
                .filter(|tab| *tab != own_tab && !tab.as_str().is_empty())
                .cloned()
                .collect(),
        }
    }

    /// Whether `tab` holds a page the person attached, which a call may read
    /// and may not act on.
    pub fn holds(&self, tab: &TabId) -> bool {
        self.tabs.contains(tab)
    }
}

/// The projection one turn was built from.
#[derive(Clone, Debug, PartialEq)]
pub struct TurnPage {
    tab: TabId,
    handles: HandleTable,
    pub(super) shape: RenderShape,
    documents: Vec<CurrentDocument>,
    discovery_tab: Option<TabId>,
}

impl TurnPage {
    /// The page a turn was rendered from, with the numbers it issued.
    pub const fn new(tab: TabId, handles: HandleTable, shape: RenderShape) -> Self {
        Self {
            tab,
            handles,
            shape,
            documents: Vec::new(),
            discovery_tab: None,
        }
    }

    /// The same page, knowing the tab the browser opened for this task.
    ///
    /// Absent is the ordinary answer: a research task, a task whose consent
    /// granted no discovery, and one restored without its tab.
    #[must_use]
    pub fn with_discovery_tab(mut self, discovery_tab: Option<TabId>) -> Self {
        self.discovery_tab = discovery_tab;
        self
    }

    /// The same page, knowing which document each observed tab holds now.
    ///
    /// A page built without them answers [`Self::names_a_page_its_tab_left`]
    /// with `false` for every number, which is how it behaved before it knew.
    #[must_use]
    pub fn with_current_documents(mut self, documents: Vec<CurrentDocument>) -> Self {
        self.documents = documents;
        self
    }

    /// Whether `node` was read from a document its tab's root frame has since
    /// replaced (decision 0191).
    ///
    /// Numbers are task-global and outlive the page they were read from, so a
    /// model can name one from a page its tab has left. On a phone, Grok named
    /// `6`, `7` and `8` — a search page's voice-search image, clear button and
    /// search box — ten times while it was on later pages. Each reached the
    /// browser and came back `at=page-epoch` as "the page changed since it was
    /// read; read it again", so it read again and named the same number.
    /// Refused here, it is "that number is no longer available", which is
    /// true, and is the answer to a number it was never shown (verification
    /// report, section 2.48). A number from an earlier reading of the same
    /// document is still admitted (decision 0188).
    pub fn names_a_page_its_tab_left(&self, node: &NodeHandle) -> bool {
        self.documents.iter().any(|document| {
            document.tab == node.tab_id
                && document.frame == node.frame_id
                && document.page_epoch != node.page_epoch
        })
    }

    /// The tab the projection was taken from.
    ///
    /// A call that designates no node acts here. The model is never told what
    /// this is: it is the tab the page it is reading came from, carried
    /// alongside the projection rather than inside it.
    ///
    /// With several pages current it is the one whose browser-issued source
    /// identity sorts lowest, and that identity is a random UUID. So it is not
    /// a name for the person's page: once the task's own tab holds a page too,
    /// this is either of the two by chance. [`PersonsPages`] is the answer to
    /// that question (decision 0237).
    pub const fn tab(&self) -> &TabId {
        &self.tab
    }

    /// The tab the browser opened for this task, for as long as it lives.
    ///
    /// Distinct from [`Self::tab`] on purpose, and the distinction is the
    /// whole point: an errand started from a page has a page to read and a
    /// tab of its own to go looking from, and the two are different tabs.
    /// Every move acts here (decision 0224).
    pub const fn discovery_tab(&self) -> Option<&TabId> {
        self.discovery_tab.as_ref()
    }

    /// The task's own tab, once a document of its own is standing in it.
    ///
    /// [`Self::tab`] is the first source in order, which for an errand asked
    /// from a page was taken to be the person's page for as long as the task
    /// runs — true while that page is the only one current, and a coin toss
    /// after (see [`Self::tab`]). So a call that names no node — a page read, a
    /// query — went back to the page the errand was asked from, every turn,
    /// and the site the task had just opened was read exactly once and never
    /// again. Measured on a phone: a search landed a results page (88 nodes,
    /// 7 openable links), and the fourteen reads that followed were the same
    /// 16-node, 1305-byte document, byte for byte, over two and a half
    /// minutes. The task could not see where it had gone.
    ///
    /// The document test is the whole of the condition. A move may act on
    /// this tab while it is still blank, because landing something in it is
    /// what a move is for; a read may not, because a blank tab is not a page
    /// and the person's page is the only thing there is to read until one
    /// lands (decision 0225).
    pub fn own_tab_holding_a_page(&self) -> Option<&TabId> {
        let own_tab = self.discovery_tab.as_ref()?;
        self.documents
            .iter()
            .any(|document| document.tab == *own_tab)
            .then_some(own_tab)
    }

    /// The numbers this projection issued.
    pub const fn handles(&self) -> &HandleTable {
        &self.handles
    }

    /// The shape of the projection, for the durable record.
    pub const fn shape(&self) -> RenderShape {
        self.shape
    }
}

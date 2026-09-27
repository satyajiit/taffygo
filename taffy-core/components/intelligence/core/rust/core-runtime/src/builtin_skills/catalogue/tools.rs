// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Required-tool rows for the compiled built-in catalogue.

use crate::builtin_skills::{PlannedTool, RequiredTool};

const fn registered(name: &'static str) -> RequiredTool {
    RequiredTool::Registered(name)
}

const fn planned(tool: PlannedTool) -> RequiredTool {
    RequiredTool::Planned(tool)
}

pub(super) const GENERAL: &[RequiredTool] = &[
    registered("browser.search"),
    registered("browser.navigate"),
    registered("browser.tabs.open"),
    registered("browser.tabs.list"),
    registered("browser.tabs.activate"),
    registered("browser.tabs.close"),
    registered("browser.dom.query"),
    registered("browser.dom.read"),
    registered("browser.link.open"),
    registered("browser.dom.scroll"),
    registered("user.ask"),
    registered("user.handover"),
];
pub(super) const DEEP: &[RequiredTool] = &[
    registered("browser.search"),
    registered("browser.navigate"),
    registered("browser.tabs.open"),
    registered("browser.tabs.list"),
    registered("browser.tabs.activate"),
    registered("browser.tabs.close"),
    registered("browser.dom.query"),
    registered("browser.dom.read"),
    registered("browser.link.open"),
    registered("browser.dom.scroll"),
    registered("user.ask"),
    registered("user.handover"),
    registered("run.spawn"),
    registered("library.search"),
];
pub(super) const PRODUCT: &[RequiredTool] = &[
    registered("browser.search"),
    registered("browser.navigate"),
    registered("browser.tabs.open"),
    registered("browser.tabs.list"),
    registered("browser.tabs.activate"),
    registered("browser.tabs.close"),
    registered("browser.dom.query"),
    registered("browser.dom.read"),
    registered("browser.link.open"),
    registered("artifact.csv.create"),
    registered("artifact.xlsx.create"),
    registered("user.ask"),
    registered("user.handover"),
];
pub(super) const MULTI: &[RequiredTool] = &[
    registered("browser.tabs.list"),
    registered("browser.tabs.activate"),
    registered("browser.dom.query"),
    registered("browser.dom.read"),
    registered("artifact.csv.create"),
    registered("artifact.markdown.create"),
    registered("user.ask"),
    registered("user.handover"),
];
pub(super) const SUMMARY: &[RequiredTool] = &[
    registered("browser.dom.read"),
    registered("browser.dom.query"),
    registered("browser.selection.read"),
    registered("page.screenshot.inspect"),
    registered("artifact.markdown.create"),
    registered("library.save"),
    registered("user.ask"),
];
pub(super) const PDF: &[RequiredTool] = &[
    registered("page.pdf.inspect"),
    registered("browser.dom.read"),
    registered("browser.dom.query"),
    registered("browser.selection.read"),
    registered("artifact.markdown.create"),
    registered("library.save"),
    registered("user.ask"),
];
pub(super) const DATA: &[RequiredTool] = &[
    registered("browser.dom.read"),
    registered("browser.dom.query"),
    registered("core.table.reshape"),
    registered("artifact.csv.create"),
    registered("artifact.xlsx.create"),
    registered("library.save"),
    registered("user.ask"),
];
pub(super) const FORM: &[RequiredTool] = &[
    registered("browser.dom.read"),
    registered("browser.dom.query"),
    registered("browser.dom.focus"),
    registered("browser.dom.click"),
    registered("browser.form.inspect"),
    registered("browser.form.fill"),
    registered("browser.form.select"),
    registered("browser.form.toggle"),
    registered("browser.form.submit"),
    registered("user.request_values"),
    registered("user.ask"),
    registered("user.handover"),
];
pub(super) const SHOPPING: &[RequiredTool] = &[
    registered("browser.search"),
    registered("browser.navigate"),
    registered("browser.tabs.open"),
    registered("browser.tabs.list"),
    registered("browser.tabs.activate"),
    registered("browser.tabs.close"),
    registered("browser.dom.query"),
    registered("browser.dom.read"),
    registered("browser.link.open"),
    registered("artifact.csv.create"),
    registered("user.ask"),
    registered("user.handover"),
];
pub(super) const DOWNLOAD: &[RequiredTool] = &[
    registered("browser.download.start"),
    registered("browser.download.from_link"),
    registered("browser.download.list"),
    registered("browser.download.cancel"),
    planned(PlannedTool::DownloadClassify),
    planned(PlannedTool::DownloadRename),
    planned(PlannedTool::DownloadMove),
    planned(PlannedTool::DownloadUndo),
    registered("user.ask"),
    registered("user.handover"),
];
pub(super) const TRAVEL: &[RequiredTool] = &[
    registered("browser.search"),
    registered("browser.navigate"),
    registered("browser.tabs.open"),
    registered("browser.tabs.list"),
    registered("browser.tabs.activate"),
    registered("browser.tabs.close"),
    registered("browser.dom.query"),
    registered("browser.dom.read"),
    registered("browser.link.open"),
    registered("browser.selection.read"),
    registered("artifact.markdown.create"),
    registered("artifact.csv.create"),
    registered("user.ask"),
    registered("run.spawn"),
];
pub(super) const VIDEO: &[RequiredTool] = &[
    registered("page.video.inspect"),
    registered("media.probe"),
    registered("media.audio.extract"),
    registered("media.frames.sample"),
    registered("media.transcode"),
    registered("browser.dom.read"),
    registered("browser.dom.query"),
    registered("artifact.markdown.create"),
    registered("user.ask"),
];
pub(super) const IMAGE: &[RequiredTool] = &[
    registered("page.images"),
    registered("page.screenshot.inspect"),
    registered("browser.dom.read"),
    registered("browser.dom.query"),
    registered("artifact.markdown.create"),
    registered("user.ask"),
];
pub(super) const LIBRARY: &[RequiredTool] = &[
    registered("library.search"),
    registered("library.save"),
    registered("library.remove"),
    registered("browser.dom.read"),
    registered("browser.selection.read"),
    registered("user.ask"),
];
pub(super) const SHEET: &[RequiredTool] = &[
    registered("browser.dom.read"),
    registered("browser.dom.query"),
    registered("core.table.reshape"),
    registered("artifact.csv.create"),
    registered("artifact.xlsx.create"),
    registered("user.ask"),
];
pub(super) const DOCUMENT: &[RequiredTool] = &[
    registered("browser.dom.read"),
    registered("browser.dom.query"),
    registered("artifact.markdown.create"),
    registered("artifact.pdf.create"),
    registered("artifact.docx.create"),
    registered("artifact.pptx.create"),
    registered("library.search"),
    registered("user.ask"),
];

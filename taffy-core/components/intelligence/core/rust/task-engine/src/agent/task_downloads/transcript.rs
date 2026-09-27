// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::TaskDownloadState;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TaskDownloadTranscriptEntry {
    pub handle: u32,
    pub state: TaskDownloadState,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TaskDownloadTranscriptOutcome {
    Started {
        handle: u32,
        state: TaskDownloadState,
    },
    Listed {
        downloads: Vec<TaskDownloadTranscriptEntry>,
        truncated: bool,
    },
    Cancelled,
}

impl TaskDownloadTranscriptOutcome {
    pub const fn tool_name(&self) -> &'static str {
        match self {
            Self::Started { .. } => "browser.download.start",
            Self::Listed { .. } => "browser.download.list",
            Self::Cancelled => "browser.download.cancel",
        }
    }

    pub fn matches_tool(&self, name: &str) -> bool {
        name == self.tool_name()
            || (matches!(self, Self::Started { .. }) && name == "browser.download.from_link")
    }

    /// Compiled-in words only. GUIDs, names, URLs, paths and media subtypes
    /// have no rendering path.
    pub fn result_pieces(&self) -> Vec<&'static str> {
        match self {
            Self::Started { handle, state } => {
                vec!["Download ", handle_word(*handle), " ", state.word(), "."]
            }
            Self::Cancelled => vec!["Download cancelled."],
            Self::Listed {
                downloads,
                truncated,
            } if downloads.is_empty() => vec![if *truncated {
                "No bounded download result was available."
            } else {
                "No downloads are linked to this page."
            }],
            Self::Listed {
                downloads,
                truncated,
            } => listed_pieces(downloads, *truncated),
        }
    }
}

fn listed_pieces(downloads: &[TaskDownloadTranscriptEntry], truncated: bool) -> Vec<&'static str> {
    let mut pieces = vec!["Downloads: "];
    for (index, download) in downloads.iter().enumerate() {
        if index > 0 {
            pieces.push(", ");
        }
        pieces.extend([
            "download ",
            handle_word(download.handle),
            " (",
            download.state.word(),
            ")",
        ]);
    }
    pieces.push(if truncated {
        "; more downloads were omitted by the browser bound."
    } else {
        "."
    });
    pieces
}

const fn handle_word(handle: u32) -> &'static str {
    match handle {
        1 => "one",
        2 => "two",
        3 => "three",
        4 => "four",
        5 => "five",
        6 => "six",
        7 => "seven",
        8 => "eight",
        9 => "nine",
        10 => "ten",
        11 => "eleven",
        12 => "twelve",
        13 => "thirteen",
        14 => "fourteen",
        15 => "fifteen",
        16 => "sixteen",
        _ => "unknown",
    }
}

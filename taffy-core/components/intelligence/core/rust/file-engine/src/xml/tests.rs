// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{validate, DECLARATION};
use crate::{FileError, PackageIssue};

#[test]
fn generated_xml_subset_accepts_unicode_text_and_closed_entities() {
    let document = format!("{DECLARATION}<root a=\"one &amp; two\">café &lt; tea</root>");
    assert_eq!(validate(document.as_bytes()), Ok(()));
}

#[test]
fn declarations_entities_attributes_and_nesting_are_adversarially_closed() {
    for body in [
        "<root><child></root>",
        "<root a=\"one\" a=\"two\"/>",
        "<root>&unknown;</root>",
        "<root/><second/>",
        "<!DOCTYPE root><root/>",
        "<root a=unquoted/>",
    ] {
        let document = format!("{DECLARATION}{body}");
        assert_eq!(
            validate(document.as_bytes()),
            Err(FileError::InvalidPackage(PackageIssue::InvalidXml))
        );
    }
}

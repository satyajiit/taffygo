// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::super::{
    available_at, resolve, Milestone, NameMatch, ToolAvailability, ToolDispatch, ToolLookup,
    REGISTRY,
};
use crate::authority::ActionClass;
use crate::tool::{
    call_operands_are_valid, validate, ArgumentRefusalReason, ArgumentValue, SuppliedArgument,
    ToolRuntime,
};
use policy_engine::{ActionClass as PolicyActionClass, PolicyMilestone};

#[test]
fn a_later_milestone_tool_reports_the_milestone_that_owns_it() {
    for (name, owner) in [
        ("browser.form.fill", Milestone::M5),
        ("browser.form.submit", Milestone::M5),
        ("browser.download.start", Milestone::M5),
        ("page.pdf.inspect", Milestone::M4),
        ("page.video.inspect", Milestone::M6),
        ("page.screenshot.inspect", Milestone::M6),
        ("library.search", Milestone::M6),
        ("media.probe", Milestone::M7),
        ("media.audio.extract", Milestone::M7),
        ("media.frames.sample", Milestone::M7),
        ("media.transcode", Milestone::M7),
        ("artifact.xlsx.create", Milestone::M7),
        ("core.table.reshape", Milestone::M7),
        ("python.execute", Milestone::M7),
    ] {
        match resolve(name, Milestone::M3) {
            ToolLookup::Unavailable { available_from, .. } => {
                assert_eq!(available_from, owner, "{name}");
            }
            other => unreachable!("{name} resolved to {other:?}"),
        }
    }
}

#[test]
fn media_jobs_accept_only_a_resident_handle_and_a_bounded_frame_count() {
    for name in ["media.probe", "media.audio.extract", "media.transcode"] {
        let ToolLookup::Available(entry) = resolve(name, Milestone::M7) else {
            unreachable!("{name} is available at M7")
        };
        assert_eq!(entry.dispatch, ToolDispatch::ToolJob(ToolRuntime::Media));
        assert_eq!(
            validate(
                entry.definition(),
                &[SuppliedArgument::new("source", ArgumentValue::Handle(1))],
            ),
            Ok(())
        );
        for forbidden in ["path", "url", "bytes", "flags", "codec"] {
            let Err(refusal) = validate(
                entry.definition(),
                &[
                    SuppliedArgument::new("source", ArgumentValue::Handle(1)),
                    SuppliedArgument::new(forbidden, ArgumentValue::Text("/tmp/x".to_owned())),
                ],
            ) else {
                unreachable!("{name} accepted {forbidden}")
            };
            assert_eq!(refusal.reason, ArgumentRefusalReason::UnknownArgument);
        }
    }

    let ToolLookup::Available(frames) = resolve("media.frames.sample", Milestone::M7) else {
        unreachable!("media.frames.sample is available at M7")
    };
    assert_eq!(frames.dispatch, ToolDispatch::ToolJob(ToolRuntime::Media));
    assert_eq!(
        validate(
            frames.definition(),
            &[
                SuppliedArgument::new("source", ArgumentValue::Handle(1)),
                SuppliedArgument::new("max_frames", ArgumentValue::Count(12)),
            ],
        ),
        Ok(())
    );
    assert!(!call_operands_are_valid(
        "media.frames.sample",
        &[
            SuppliedArgument::new("source", ArgumentValue::Handle(1)),
            SuppliedArgument::new("max_frames", ArgumentValue::Count(13)),
        ],
    ));
}

#[test]
fn python_is_a_closed_registered_job_and_never_accepts_source_or_code() {
    let ToolLookup::Available(entry) = resolve("python.execute", Milestone::M7) else {
        unreachable!("python.execute is available at M7")
    };
    assert_eq!(entry.dispatch, ToolDispatch::ToolJob(ToolRuntime::Python));
    let call = [
        SuppliedArgument::new("entrypoint", ArgumentValue::Choice("document".to_owned())),
        SuppliedArgument::new("title", ArgumentValue::Text("Report".to_owned())),
        SuppliedArgument::new("content", ArgumentValue::Text("Body".to_owned())),
    ];
    assert_eq!(validate(entry.definition(), &call), Ok(()));
    for forbidden in ["source", "code", "module", "path", "argv"] {
        let mut injected = call.to_vec();
        injected.push(SuppliedArgument::new(
            forbidden,
            ArgumentValue::Text("print('not data')".to_owned()),
        ));
        let Err(refusal) = validate(entry.definition(), &injected) else {
            unreachable!("{forbidden} must be refused")
        };
        assert_eq!(refusal.reason, ArgumentRefusalReason::UnknownArgument);
        assert_eq!(refusal.parameter, None);
    }
}

#[test]
fn every_rich_artifact_name_dispatches_to_its_exact_closed_kind() {
    for kind in crate::ArtifactKind::RICH {
        let name = kind.tool_name();
        let lookup = resolve(name, Milestone::M7);
        let Some(entry) = lookup.entry() else {
            unreachable!("{name} is registered")
        };
        assert!(lookup.is_available(), "{name}");
        assert_eq!(entry.dispatch, ToolDispatch::Artifact(*kind), "{name}");
        assert!(
            entry.parameters.is_empty(),
            "{name} accepts model-authored file input"
        );
    }
}

#[test]
fn the_excluded_names_are_refused_at_every_milestone() {
    for name in ["device.clipboard.read", "device.share.send"] {
        for milestone in Milestone::ALL {
            assert!(
                matches!(resolve(name, *milestone), ToolLookup::Excluded(_)),
                "{name} at {milestone}"
            );
        }
    }
}

#[test]
fn upload_has_no_model_callable_name_or_argument_seam() {
    // Decision 0089 leaves file choice with the person. Keep that absence
    // executable: adding any row that spends UploadFile authority, or a
    // plausible upload spelling, must fail here before a path or byte schema
    // can be attached to it.
    assert!(REGISTRY
        .iter()
        .all(|entry| entry.dispatch.action_class() != Some(ActionClass::UploadFile)));
    for name in [
        "browser.file.upload",
        "browser.form.upload",
        "browser.upload",
    ] {
        assert_eq!(resolve(name, Milestone::M8), ToolLookup::Unknown, "{name}");
    }
    for milestone in PolicyMilestone::ALL {
        assert!(!PolicyActionClass::UploadFile.is_authorized_at(*milestone));
    }
}

#[test]
fn no_milestone_makes_an_excluded_name_available() {
    for entry in REGISTRY {
        if entry.availability != ToolAvailability::ExcludedByRequirement {
            continue;
        }
        assert_eq!(entry.owning_milestone(), None, "{}", entry.name);
        for milestone in Milestone::ALL {
            assert!(
                !entry.is_available_at(*milestone),
                "{} at {milestone}",
                entry.name
            );
            assert!(
                !available_at(*milestone)
                    .iter()
                    .any(|listed| listed.name == entry.name),
                "{} listed at {milestone}",
                entry.name
            );
        }
    }
}

#[test]
fn an_unregistered_name_fails_closed() {
    for name in [
        "",
        "browser",
        "browser.navigate.extra",
        "browser.dom",
        "browser.dom.querySelector",
        "device.clipboard",
        "shell.exec",
        // `user.handover` and `user.ask` are exact rows, so a name
        // below either of them is not registered. `user` is unknown for a
        // second reason and stays unknown even if a `user` namespace row
        // is ever added, because a namespace claims `<prefix>.<member>`
        // and never the bare prefix — the rule
        // `a_namespace_claims_the_members_it_names_and_no_others`
        // asserts for `page.images`. Such a row would make the two
        // dotted names below resolve while `user` went on failing closed,
        // and it is that pair this list is here to notice.
        "user",
        "user.handovers",
        "user.ask.again",
    ] {
        assert_eq!(
            resolve(name, Milestone::M8),
            ToolLookup::Unknown,
            "{name} must not resolve to a neighbour"
        );
    }
}

#[test]
fn tab_members_are_exact_rows_and_the_group_name_is_not_callable() {
    assert!(matches!(
        resolve("browser.tabs.open", Milestone::M3),
        ToolLookup::Available(entry) if entry.name == "browser.tabs.open"
    ));
    for served in [
        "browser.tabs.open",
        "browser.tabs.list",
        "browser.tabs.activate",
        "browser.tabs.close",
    ] {
        assert!(
            matches!(resolve(served, Milestone::M3), ToolLookup::Available(entry) if entry.name == served),
            "{served}"
        );
    }
    assert_eq!(resolve("browser.tabs", Milestone::M3), ToolLookup::Unknown);
    assert_eq!(
        resolve("browser.tabsx.open", Milestone::M3),
        ToolLookup::Unknown
    );
}

#[test]
fn the_name_a_proposal_carries_is_the_tables_and_never_the_callers() {
    // `ActionProposal::tool_name` is journalled, so whatever produces it
    // decides what can reach a durable record. `canonical_name` is that
    // producer, and every string it returns has to be assembled from the
    // registry's own `&'static str`s rather than echoed from the argument.
    //
    // Asserted by identity against the table rather than by comparing
    // against the input: comparing to the input would pass for a function
    // that simply returned it, which is exactly the shape being ruled out.
    for entry in REGISTRY {
        let probes: Vec<String> = match entry.matching {
            NameMatch::Exact => vec![entry.name.to_owned()],
            NameMatch::Namespace { members } => {
                members.iter().map(|member| (*member).to_owned()).collect()
            }
        };
        for probe in probes {
            let named = entry
                .canonical_name(&probe)
                .unwrap_or_else(|| unreachable!("{probe} is this row's own name"));
            let built_from_table = match entry.matching {
                NameMatch::Exact => named == entry.name,
                NameMatch::Namespace { members } => members.contains(&named.as_str()),
            };
            assert!(built_from_table, "{named} was not assembled from the table");
        }
        // And a name this row does not claim yields nothing at all, so
        // there is no path that falls back to the caller's spelling.
        assert!(entry
            .canonical_name(&format!("{}.not-a-member-xyz", entry.name))
            .is_none());
    }
}

#[test]
fn a_namespace_claims_the_members_it_names_and_no_others() {
    // This case used to be asserted the other way round: `page.images.a.b`
    // was listed beside the real members as a name that *should* resolve,
    // because the match was a prefix with an open tail. That is not a
    // harmless permissiveness. `ActionProposal::tool_name` is journalled,
    // and the name the model asked for was what got written — so
    // `page.images.` followed by whatever a model chose to put there was
    // a dispatched call and a permanent record of model-authored text, in
    // a journal that is supposed to retain counts and closed enumerations
    // and nothing a page or a model wrote. A one-time code fits easily.
    //
    // The member list closes it, and this is the test that says so.
    for invented in [
        "page.images.a.b",
        "page.images.caption",
        "page.images.SECRET-otp-123456",
        "page.images.",
    ] {
        assert_eq!(
            resolve(invented, Milestone::M8),
            ToolLookup::Unknown,
            "{invented} is not a member and must not resolve"
        );
    }
    assert_eq!(resolve("page.images", Milestone::M8), ToolLookup::Unknown);
    for served in ["page.images.describe", "page.images.read_text"] {
        assert!(matches!(
            resolve(served, Milestone::M6),
            ToolLookup::Available(entry) if entry.name == "page.images"
        ));
    }
}

#[test]
fn no_earlier_row_shadows_a_later_name() {
    // `resolve` takes the first row that matches, so registration order is
    // load-bearing: a namespace row placed above a name it prefixes
    // swallows every such row below it. The shadowed rows keep their
    // milestone and their idempotency class in review and never reach a
    // caller, which is the worst shape a table can fail in — the reviewer
    // reads a definition that the product does not use. Registering `user`
    // as a namespace above `user.handover` and `user.ask` would do exactly
    // that to both, so the property is asserted over every pair rather
    // than over the pair that prompted it.
    for (index, entry) in REGISTRY.iter().enumerate() {
        let claimed = match entry.matching {
            NameMatch::Exact => entry.name.to_owned(),
            // A namespace row claims `<prefix>.<member>` and not the
            // prefix itself, so it has to be probed with a member.
            // A member the row actually names. Fabricating one used to
            // resolve, which is the defect the closed member list fixes,
            // so probing with a made-up member would now test nothing.
            NameMatch::Namespace { members } => members
                .first()
                .map_or_else(|| entry.name.to_owned(), |member| (*member).to_owned()),
        };
        for earlier in REGISTRY.iter().take(index) {
            assert!(
                !earlier.matches(&claimed),
                "{} is registered above {} and shadows it",
                earlier.name,
                entry.name
            );
        }
    }
}

#[test]
fn every_registered_name_is_unique_and_lowercase_dotted() {
    let mut seen: Vec<&str> = Vec::new();
    for entry in REGISTRY {
        assert!(
            !seen.contains(&entry.name),
            "{} is registered twice",
            entry.name
        );
        seen.push(entry.name);
        assert!(entry.name.contains('.'), "{} is not dotted", entry.name);
        assert!(
            entry
                .name
                .chars()
                .all(|character| character.is_ascii_lowercase() || matches!(character, '.' | '_')),
            "{} is not a lowercase dotted name",
            entry.name
        );
        assert!(!entry.purpose.is_empty(), "{} has no purpose", entry.name);
    }
}

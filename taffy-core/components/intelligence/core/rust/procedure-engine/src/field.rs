// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a step will actually do to the field in front of it now.
//!
//! # Rule 6, and why it is a downgrade rather than a refusal
//!
//! A procedure is a claim about a page it saw before. A field whose purpose the
//! classifier cannot name is the page disagreeing with that claim — and the
//! honest response to a disagreement is not to guess, and not to abandon the
//! task either. The step is handed to the person, who is looking at the same
//! field and can say what it is for.
//!
//! That is why this is [`StepDisposition::HandToUser`] and not a load-time
//! refusal: [`FieldPurpose::Unknown`] is a fact about *this* replay, produced by
//! the classifier at the moment the step is about to run. A procedure whose
//! recorded purpose is `Unknown` is refused nothing at storage — it is simply a
//! procedure whose fill will always be handed over, which is the correct
//! outcome and a visible one.
//!
//! # Three ways a fill loses its right to run, and all three end in one place
//!
//! - The **record** could not name what it was filling.
//! - The **page** cannot be classified now.
//! - The two disagree: the record says one thing and the field in front of the
//!   step is something else.
//!
//! The third is the one worth stating separately, and it is the reason
//! [`disposition`] takes the observed purpose as an argument rather than reading
//! the record alone. A form that gained a field, or a page shaped to resemble
//! the recorded one, moves what the recorded step would fill; a check that only
//! asked "did the record name a purpose" would answer yes and type a person's
//! details into whatever is there now.
//!
//! # A purpose is a classification and never a value
//!
//! Three members — [`FieldPurpose::NationalIdentifier`],
//! [`FieldPurpose::ChallengeAnswer`] and [`FieldPurpose::OneTimeCode`] — name
//! the kinds of field an errand runs into where what goes in is a thing only
//! the person has. Naming them is what lets the step ask; it is emphatically
//! not what lets the record hold one. Credentials, one-time codes, payment
//! values and passkeys never enter the AI data plane, and that invariant is
//! carried here by construction rather than by care:
//!
//! - a recorded fill's value is [`crate::step::StepValue::FromPerson`], which
//!   holds a purpose and has no field for bytes at all;
//! - the parameter it satisfies is declared `SuppliedValue`, so a literal
//!   carrying text is refused by `task_engine::tool::validate` as a type
//!   mismatch before any rule of this crate looks at it
//!   (`no_fill_can_carry_the_bytes_of_what_the_person_typed`);
//! - and every member here is a compiled-in name, so a purpose shown to a
//!   person is the build's word rather than a page's.
//!
//! The vocabulary stays generic for the same reason the phrase catalogue does.
//! A member named for one country's scheme, one issuer or one vendor would be
//! a product claim about where an errand runs, made in a table nobody reviews
//! as one.

use crate::step::ProcedureStep;

/// What a field is for, as the classifier names it.
///
/// A closed vocabulary, and small on purpose. It is not a list of every field a
/// form can have — it is the list this build is prepared to *act* on without
/// asking. Anything outside it is [`Self::Unknown`], and `Unknown` is a
/// perfectly good answer rather than a failure of the classifier: a field
/// nobody can name is a field a person can be asked about.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FieldPurpose {
    /// A person's given name.
    GivenName,
    /// A person's family name.
    FamilyName,
    /// A whole name in one field.
    FullName,
    /// An email address.
    EmailAddress,
    /// A telephone number.
    TelephoneNumber,
    /// A street address line.
    StreetAddress,
    /// A town or city.
    Locality,
    /// A postal code.
    PostalCode,
    /// A country.
    Country,
    /// An organization's name.
    OrganizationName,
    /// What to search for.
    SearchTerms,
    /// A count of something.
    Quantity,
    /// A date.
    CalendarDate,
    /// A note the person writes in their own words.
    FreeTextNote,
    /// An identifier a country issues to a person and its public services ask
    /// for.
    ///
    /// Generic on purpose: one member for the shape rather than one per
    /// country or per scheme. What an errand needs is to know that the field
    /// in front of it is this kind of thing, so that it asks instead of
    /// composing; which country's it is is the person's business and the
    /// page's, and a member naming one would be this table making a product
    /// claim about where the browser is used.
    NationalIdentifier,
    /// An answer only the person knows, asked to check that it is them.
    ///
    /// Classifying it is the opposite of solving it. Taffy never learns to
    /// recognise a challenge meant to prove a person is present — decision
    /// 0055 section 8 makes exhaustion the mechanism and the action class is
    /// prohibited — so naming the field is exactly what routes it to the one
    /// party who may answer.
    ChallengeAnswer,
    /// A code that is good once and briefly.
    OneTimeCode,
    /// The classifier could not name it.
    Unknown,
}

impl FieldPurpose {
    /// Every member, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::GivenName,
        Self::FamilyName,
        Self::FullName,
        Self::EmailAddress,
        Self::TelephoneNumber,
        Self::StreetAddress,
        Self::Locality,
        Self::PostalCode,
        Self::Country,
        Self::OrganizationName,
        Self::SearchTerms,
        Self::Quantity,
        Self::CalendarDate,
        Self::FreeTextNote,
        Self::NationalIdentifier,
        Self::ChallengeAnswer,
        Self::OneTimeCode,
        Self::Unknown,
    ];

    /// A short, compiled-in name, safe to record in an audit event and safe to
    /// show a person as the name of what they are being asked for.
    pub const fn label(self) -> &'static str {
        match self {
            Self::GivenName => "given_name",
            Self::FamilyName => "family_name",
            Self::FullName => "full_name",
            Self::EmailAddress => "email_address",
            Self::TelephoneNumber => "telephone_number",
            Self::StreetAddress => "street_address",
            Self::Locality => "locality",
            Self::PostalCode => "postal_code",
            Self::Country => "country",
            Self::OrganizationName => "organization_name",
            Self::SearchTerms => "search_terms",
            Self::Quantity => "quantity",
            Self::CalendarDate => "calendar_date",
            Self::FreeTextNote => "free_text_note",
            Self::NationalIdentifier => "national_identifier",
            Self::ChallengeAnswer => "challenge_answer",
            Self::OneTimeCode => "one_time_code",
            Self::Unknown => "unknown",
        }
    }

    /// Whether the classifier named this field.
    pub const fn is_named(self) -> bool {
        !matches!(self, Self::Unknown)
    }
}

/// Why a step is being handed to the person instead of run.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum HandoverReason {
    /// One side of the comparison could not name the field.
    Unclassified,
    /// Both sides named it, and named different things.
    PurposeChanged,
}

impl HandoverReason {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Unclassified => "unclassified",
            Self::PurposeChanged => "purpose_changed",
        }
    }
}

/// What a step does when it is reached.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum StepDisposition {
    /// The step is proposed as recorded — and then decided, minted, dispatched
    /// and verified exactly as any other proposal is.
    Replay,
    /// The step is not proposed. The person is asked instead.
    HandToUser(HandoverReason),
}

impl StepDisposition {
    /// Whether this step will be proposed at all.
    pub const fn is_replay(self) -> bool {
        matches!(self, Self::Replay)
    }

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Replay => "replay",
            Self::HandToUser(reason) => reason.label(),
        }
    }
}

/// What the classifier says about the field each step of one procedure is
/// about, **now**.
///
/// One purpose per step, in the procedure's own step order, because rule 6 is
/// a comparison and a comparison needs both halves: the record says what the
/// step was recorded filling and this says what is in front of it today.
///
/// # A missing entry is `Unknown`, and that is the safe direction
///
/// [`Self::at`] answers [`FieldPurpose::Unknown`] for a step this carries
/// nothing about, so a caller that classified some fields and not others hands
/// the rest to the person rather than replaying them unchecked. [`Self::none`]
/// is therefore a complete and honest value rather than a placeholder: it says
/// "nothing here has been classified", and every fill of every procedure is
/// handed over under it.
///
/// It is what every caller passes today. `FieldPurpose` exists in this crate
/// and in no contract, so there is no seam a freshly classified purpose can
/// cross from the browser process — see [`crate::replay::next_procedure_command`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ObservedFields<'a> {
    purposes: &'a [FieldPurpose],
}

impl<'a> ObservedFields<'a> {
    /// Nothing about this page has been classified.
    pub const fn none() -> Self {
        Self { purposes: &[] }
    }

    /// One purpose per step of the procedure, in step order.
    pub const fn per_step(purposes: &'a [FieldPurpose]) -> Self {
        Self { purposes }
    }

    /// What the classifier says about the field the `index`-th step is about.
    pub fn at(self, index: usize) -> FieldPurpose {
        self.purposes
            .get(index)
            .copied()
            .unwrap_or(FieldPurpose::Unknown)
    }
}

/// What `step` does, given what the classifier says about the field in front of
/// it now.
///
/// `observed` is ignored for a step that fills nothing, because there is no
/// field for it to be about. It is deliberately still a required argument: a
/// signature that took it only for fills would need the caller to know which
/// steps are fills, and that is exactly the judgement this function exists to
/// make.
pub fn disposition(step: &ProcedureStep, observed: FieldPurpose) -> StepDisposition {
    // A step is a fill for this rule if *either* half says so, and only a step
    // where both halves say no is left alone. `validate` refuses a record where
    // the two disagree, so this is what the rule answers when an unvalidated one
    // reaches here anyway — and the direction is deliberate: reading a fill verb
    // with no recorded purpose as "not a fill" would replay it, which is the
    // permissive reading of a record nothing checked.
    if !step.is_a_fill() && step.fills.is_none() {
        return StepDisposition::Replay;
    }
    let recorded = step.fills.unwrap_or(FieldPurpose::Unknown);
    if !recorded.is_named() || !observed.is_named() {
        return StepDisposition::HandToUser(HandoverReason::Unclassified);
    }
    if recorded != observed {
        return StepDisposition::HandToUser(HandoverReason::PurposeChanged);
    }
    StepDisposition::Replay
}

#[cfg(test)]
mod tests {
    use super::{disposition, FieldPurpose, HandoverReason, ObservedFields, StepDisposition};
    use crate::step::ProcedureStep;
    use bip_types::action::PostconditionKind;

    fn fill(purpose: FieldPurpose) -> ProcedureStep {
        ProcedureStep::new("browser.form.fill", PostconditionKind::NodeValueChanged)
            .filling(purpose)
    }

    #[test]
    fn an_unclassified_field_is_handed_to_the_person_from_either_side() {
        // The record could not name it.
        assert_eq!(
            disposition(&fill(FieldPurpose::Unknown), FieldPurpose::EmailAddress),
            StepDisposition::HandToUser(HandoverReason::Unclassified)
        );
        // The page cannot be classified now.
        assert_eq!(
            disposition(&fill(FieldPurpose::EmailAddress), FieldPurpose::Unknown),
            StepDisposition::HandToUser(HandoverReason::Unclassified)
        );
        // Neither side can.
        assert_eq!(
            disposition(&fill(FieldPurpose::Unknown), FieldPurpose::Unknown),
            StepDisposition::HandToUser(HandoverReason::Unclassified)
        );
    }

    #[test]
    fn an_unclassified_field_is_never_guessed_at_for_any_recorded_purpose() {
        // The rule is about the whole vocabulary and not about the member
        // somebody happened to write a test for: a fill against a field the
        // classifier cannot name is handed over whatever the record recorded.
        for purpose in FieldPurpose::ALL {
            assert!(
                !disposition(&fill(*purpose), FieldPurpose::Unknown).is_replay(),
                "{}",
                purpose.label()
            );
        }
    }

    #[test]
    fn a_field_that_became_something_else_is_handed_over_and_not_typed_into() {
        assert_eq!(
            disposition(
                &fill(FieldPurpose::PostalCode),
                FieldPurpose::TelephoneNumber
            ),
            StepDisposition::HandToUser(HandoverReason::PurposeChanged)
        );
    }

    #[test]
    fn a_fill_the_page_agrees_with_replays() {
        for purpose in FieldPurpose::ALL.iter().filter(|entry| entry.is_named()) {
            assert_eq!(
                disposition(&fill(*purpose), *purpose),
                StepDisposition::Replay,
                "{}",
                purpose.label()
            );
        }
    }

    #[test]
    fn a_fill_verb_with_no_recorded_purpose_is_handed_over_and_not_replayed() {
        // `validate` refuses this record, so it can only arrive from a caller
        // that skipped validation — and the answer then has to fall the safe
        // way. Reading "no recorded purpose" as "not a fill" would have typed
        // into the field instead.
        let unrecorded =
            ProcedureStep::new("browser.form.fill", PostconditionKind::NodeValueChanged);
        assert_eq!(unrecorded.fills, None);
        for observed in FieldPurpose::ALL {
            assert_eq!(
                disposition(&unrecorded, *observed),
                StepDisposition::HandToUser(HandoverReason::Unclassified),
                "{}",
                observed.label()
            );
        }
    }

    #[test]
    fn a_step_that_fills_nothing_is_unaffected_by_the_classifier() {
        let step = ProcedureStep::new("browser.dom.read", PostconditionKind::NoMutation);
        for observed in FieldPurpose::ALL {
            assert_eq!(
                disposition(&step, *observed),
                StepDisposition::Replay,
                "{}",
                observed.label()
            );
        }
    }

    #[test]
    fn the_purposes_an_errand_needs_are_named_generically() {
        // Generic on purpose: one member per *shape* of field, never one per
        // country, scheme, issuer or vendor. A member that named one would be
        // this table making a product claim about where the browser is used,
        // in a list nobody reviews as a product decision. The assertion is
        // over every label rather than over the three added for an errand,
        // because the rule is about the vocabulary and not about the newest
        // rows in it.
        for purpose in FieldPurpose::ALL {
            let label = purpose.label();
            assert!(
                label
                    .chars()
                    .all(|character| character.is_ascii_lowercase() || character == '_'),
                "{label}"
            );
        }
        for shape in [
            FieldPurpose::NationalIdentifier,
            FieldPurpose::ChallengeAnswer,
            FieldPurpose::OneTimeCode,
        ] {
            assert!(shape.is_named());
            assert!(FieldPurpose::ALL.contains(&shape), "{}", shape.label());
            // Each is a field a person fills, so each is a fill the page has
            // to agree with before anything is typed into it.
            assert_eq!(disposition(&fill(shape), shape), StepDisposition::Replay);
            assert_eq!(
                disposition(&fill(shape), FieldPurpose::Unknown),
                StepDisposition::HandToUser(HandoverReason::Unclassified)
            );
        }
    }

    #[test]
    fn a_step_nothing_was_classified_for_is_handed_over_rather_than_replayed() {
        // `ObservedFields::none` is what every caller passes today, and it has
        // to be a complete answer rather than a placeholder: a step with no
        // classification against it is a step whose page half is missing, and
        // the missing half reads as `Unknown`.
        let nothing = ObservedFields::none();
        for index in [0_usize, 1, 31, usize::MAX] {
            assert_eq!(nothing.at(index), FieldPurpose::Unknown);
        }
        let some = [FieldPurpose::EmailAddress, FieldPurpose::PostalCode];
        let observed = ObservedFields::per_step(&some);
        assert_eq!(observed.at(0), FieldPurpose::EmailAddress);
        assert_eq!(observed.at(1), FieldPurpose::PostalCode);
        // Past the end is not an error and is not a guess either.
        assert_eq!(observed.at(2), FieldPurpose::Unknown);
        assert_eq!(
            disposition(&fill(FieldPurpose::EmailAddress), observed.at(2)),
            StepDisposition::HandToUser(HandoverReason::Unclassified)
        );
    }

    #[test]
    fn every_disposition_has_a_distinct_compiled_in_label() {
        let dispositions = [
            StepDisposition::Replay,
            StepDisposition::HandToUser(HandoverReason::Unclassified),
            StepDisposition::HandToUser(HandoverReason::PurposeChanged),
        ];
        let mut seen: Vec<&str> = Vec::new();
        for shape in dispositions {
            assert!(!seen.contains(&shape.label()), "{}", shape.label());
            seen.push(shape.label());
        }
    }

    #[test]
    fn every_purpose_has_a_distinct_compiled_in_label() {
        let mut seen: Vec<&str> = Vec::new();
        for purpose in FieldPurpose::ALL {
            assert!(!seen.contains(&purpose.label()), "{}", purpose.label());
            seen.push(purpose.label());
        }
        assert!(!FieldPurpose::Unknown.is_named());
        assert_eq!(
            FieldPurpose::ALL
                .iter()
                .filter(|entry| !entry.is_named())
                .count(),
            1
        );
    }
}

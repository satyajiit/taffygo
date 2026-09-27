// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Classify a person's typed answer before it can reach a model turn.
//!
//! The durable journal records only that an answer was supplied
//! (`PersistedCommand::SupplyUserInput` is unit). The words themselves live
//! here, ephemerally, as a compiled-in line the next compose can attach.

use core_service_types::MAX_USER_INPUT_ANSWER_BYTES;

/// Kept in step with `MAX_USER_INPUT_ANSWER_BYTES` on both host contracts.
pub const MAX_PERSON_ANSWER_BYTES: usize = MAX_USER_INPUT_ANSWER_BYTES;

const ORDINARY_PREFIX: &str = "the person answered: ";
const WITHHELD: &str = "the person answered with an identifier; the digits are withheld";
const FOLLOW_UP_PREFIX: &str = "the person asked next: ";
const WITHHELD_FOLLOW_UP: &str =
    "the person asked next about an identifier; the digits are withheld";

/// Why a typed answer cannot become the next turn's opening line.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PersonAnswerRefusal {
    /// Nothing remained after control characters were stripped.
    Empty,
    /// The raw answer exceeded the contract bound.
    TooLong,
    /// Digit-only and the length of an OTP or similar code.
    Credential,
}

/// Classifies `raw` into a compiled-in line a model turn may carry.
///
/// Four-to-eight digits, ignoring spaces and hyphens, are refused: those are
/// the shape of an OTP, and the person should type them on the page. Nine or
/// more digits of the same shape are an identifier; the digits never leave.
/// Everything else is copied, control characters stripped, behind a
/// compiled-in prefix.
pub fn classify_person_answer(raw: &str) -> Result<String, PersonAnswerRefusal> {
    if raw.len() > MAX_PERSON_ANSWER_BYTES {
        return Err(PersonAnswerRefusal::TooLong);
    }
    let sanitized = sanitize(raw);
    if sanitized.is_empty() {
        return Err(PersonAnswerRefusal::Empty);
    }
    match digit_run(&sanitized) {
        Some(count) if (4..=8).contains(&count) => Err(PersonAnswerRefusal::Credential),
        Some(count) if count >= 9 => Ok(WITHHELD.to_owned()),
        _ => Ok(format!("{ORDINARY_PREFIX}{sanitized}")),
    }
}

/// Classifies a follow-up question the person asked of a finished task
/// (decision 0137) into the line the next turn opens with.
///
/// The same sanitizer and the same digit rules as [`classify_person_answer`]
/// — a follow-up is typed on trusted chrome exactly as an answer is, and an
/// OTP-shaped one is refused for the same reason — behind a prefix that says
/// what it is, so the model reads a new question rather than an answer to one
/// it asked.
pub fn classify_follow_up_question(raw: &str) -> Result<String, PersonAnswerRefusal> {
    if raw.len() > MAX_PERSON_ANSWER_BYTES {
        return Err(PersonAnswerRefusal::TooLong);
    }
    let sanitized = sanitize(raw);
    if sanitized.is_empty() {
        return Err(PersonAnswerRefusal::Empty);
    }
    match digit_run(&sanitized) {
        Some(count) if (4..=8).contains(&count) => Err(PersonAnswerRefusal::Credential),
        Some(count) if count >= 9 => Ok(WITHHELD_FOLLOW_UP.to_owned()),
        _ => Ok(format!("{FOLLOW_UP_PREFIX}{sanitized}")),
    }
}

const WITHHELD_ASK: &str = "the question named an identifier; the digits are withheld";

/// Classifies a `user.ask` subject before it can be shown on trusted chrome.
///
/// Same sanitizer as [`classify_person_answer`]: control characters stripped,
/// OTP-shaped values refused, identifiers withheld. The person reads this
/// line, so it is not prefixed. The journal never holds it.
pub fn classify_ask_subject(raw: &str) -> Result<String, PersonAnswerRefusal> {
    if raw.len() > MAX_PERSON_ANSWER_BYTES {
        return Err(PersonAnswerRefusal::TooLong);
    }
    let sanitized = sanitize(raw);
    if sanitized.is_empty() {
        return Err(PersonAnswerRefusal::Empty);
    }
    match digit_run(&sanitized) {
        Some(count) if (4..=8).contains(&count) => Err(PersonAnswerRefusal::Credential),
        Some(count) if count >= 9 => Ok(WITHHELD_ASK.to_owned()),
        _ => Ok(sanitized),
    }
}

/// The classified `user.ask` subject still sitting on this turn's residency.
///
/// `None` when the wait is not an ask, when the subject was OTP-shaped, or
/// when the residency has already been dropped.
pub fn ask_subject_from_residency(residency: &task_engine::TurnResidency) -> Option<String> {
    for sequence in 0..residency.call_count() {
        let call = residency.call(sequence)?;
        if call.tool_name != task_engine::tool::ASK_TOOL {
            continue;
        }
        for argument in &call.arguments {
            if argument.name != "subject" {
                continue;
            }
            let task_engine::ArgumentValue::Text(text) = &argument.value else {
                continue;
            };
            return classify_ask_subject(text).ok();
        }
    }
    None
}

fn sanitize(raw: &str) -> String {
    raw.chars()
        .filter(|character| !character.is_control())
        .collect::<String>()
        .trim()
        .to_owned()
}

/// Digit count when `text` is only ASCII digits, spaces, and hyphens.
fn digit_run(text: &str) -> Option<usize> {
    let mut digits = 0usize;
    for character in text.chars() {
        match character {
            '0'..='9' => digits = digits.saturating_add(1),
            ' ' | '-' => {}
            _ => return None,
        }
    }
    (digits > 0).then_some(digits)
}

#[cfg(test)]
mod tests {
    use super::{
        classify_ask_subject, classify_follow_up_question, classify_person_answer,
        PersonAnswerRefusal, MAX_PERSON_ANSWER_BYTES,
    };

    #[test]
    fn a_follow_up_is_prefixed_as_a_question_and_not_as_an_answer() {
        assert_eq!(
            classify_follow_up_question("and the fee?").as_deref(),
            Ok("the person asked next: and the fee?")
        );
    }

    #[test]
    fn a_follow_up_keeps_the_credential_and_identifier_rules() {
        assert_eq!(
            classify_follow_up_question("123456"),
            Err(PersonAnswerRefusal::Credential)
        );
        assert_eq!(
            classify_follow_up_question("1234 5678 9012").as_deref(),
            Ok("the person asked next about an identifier; the digits are withheld")
        );
        assert_eq!(
            classify_follow_up_question(" \u{0007} "),
            Err(PersonAnswerRefusal::Empty)
        );
    }

    #[test]
    fn an_ordinary_answer_is_prefixed() {
        assert_eq!(
            classify_person_answer("the blue one").as_deref(),
            Ok("the person answered: the blue one")
        );
    }

    #[test]
    fn a_six_digit_code_is_refused() {
        assert_eq!(
            classify_person_answer("123456"),
            Err(PersonAnswerRefusal::Credential)
        );
        assert_eq!(
            classify_person_answer("12 34 56"),
            Err(PersonAnswerRefusal::Credential)
        );
    }

    #[test]
    fn a_twelve_digit_identifier_is_withheld() {
        assert_eq!(
            classify_person_answer("1234 5678 9012").as_deref(),
            Ok("the person answered with an identifier; the digits are withheld")
        );
    }

    #[test]
    fn control_characters_do_not_survive() {
        assert_eq!(
            classify_person_answer("yes\u{0007}").as_deref(),
            Ok("the person answered: yes")
        );
    }

    #[test]
    fn empty_and_oversized_answers_are_refused() {
        assert_eq!(
            classify_person_answer("   "),
            Err(PersonAnswerRefusal::Empty)
        );
        let oversized = "a".repeat(MAX_PERSON_ANSWER_BYTES.saturating_add(1));
        assert_eq!(
            classify_person_answer(&oversized),
            Err(PersonAnswerRefusal::TooLong)
        );
    }

    #[test]
    fn an_ordinary_question_is_shown_without_a_prefix() {
        assert_eq!(
            classify_ask_subject("which of the two?").as_deref(),
            Ok("which of the two?")
        );
    }

    #[test]
    fn an_otp_shaped_question_is_refused() {
        assert_eq!(
            classify_ask_subject("123456"),
            Err(PersonAnswerRefusal::Credential)
        );
    }

    #[test]
    fn an_identifier_shaped_question_is_withheld() {
        assert_eq!(
            classify_ask_subject("1234 5678 9012").as_deref(),
            Ok("the question named an identifier; the digits are withheld")
        );
    }

    fn residency_asking(subject: &str) -> task_engine::TurnResidency {
        use bip_types::identity::TabId;
        use task_engine::{
            ArgumentValue, HandleTable, ModelCallId, ModelReply, ModelStopReason, ModelToolCall,
            RenderShape, SuppliedArgument, TurnPage, TurnResidency, TurnUsage,
        };
        let reply = ModelReply {
            stop: ModelStopReason::ToolCall,
            overflow: None,
            usage: TurnUsage::default(),
            answer_segments: 0,
            tool_calls: vec![ModelToolCall::new(
                "user.ask",
                vec![SuppliedArgument::new(
                    "subject",
                    ArgumentValue::Text(subject.to_owned()),
                )],
            )],
        };
        let page = TurnPage::new(
            TabId::new("tab_1"),
            HandleTable::new(),
            RenderShape::empty([0_u8; 32]),
        );
        TurnResidency::read(ModelCallId::new("model-task-1"), page, reply).expect("one call")
    }

    #[test]
    fn the_ask_subject_is_read_from_the_resident_call() {
        assert_eq!(
            super::ask_subject_from_residency(&residency_asking("which of the two?")).as_deref(),
            Some("which of the two?")
        );
    }

    #[test]
    fn an_otp_shaped_ask_subject_is_not_shown() {
        assert_eq!(
            super::ask_subject_from_residency(&residency_asking("123456")),
            None
        );
    }
}

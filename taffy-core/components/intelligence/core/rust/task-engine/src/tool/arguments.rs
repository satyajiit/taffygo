// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deciding whether a set of supplied arguments matches a tool's schema.
//!
//! # Everything fails closed, and nothing is coerced
//!
//! Three rules, and each of them exists because the permissive version is what
//! a caller reaches for:
//!
//! - **An argument name the definition does not declare is refused**, not
//!   ignored. A tool that quietly drops what it did not understand answers a
//!   different call from the one that was made, and the model's transcript
//!   records the call it made.
//! - **A missing required argument is refused**, not defaulted. A default is a
//!   decision, and the place to make it is the schema, where a reviewer can
//!   see it.
//! - **A value of the wrong type is refused**, never converted. The text `"3"`
//!   supplied for a count is not a count, and a count supplied for text is not
//!   text. Conversion is the step that turns a model's mistake into a
//!   plausible-looking call, and a plausible-looking call is the one that
//!   passes every later check.
//!
//! # A refusal carries no supplied text
//!
//! [`ArgumentRefusal`] names a compiled-in reason and, where the fault is about
//! a parameter the build declares, that parameter's own `&'static str` name.
//! It never carries the name or the value the caller supplied. Naming the
//! unrecognised argument back would be the one field in this crate's refusal
//! vocabulary through which model text could travel to the journal, and the
//! caller already knows what it sent.

use super::definition::{Parameter, ParameterType, ToolDefinition};

/// How many arguments one call may carry.
///
/// The declared schemas are much smaller than this, so the ceiling is not
/// about them: the supplied list comes from a model, and a scan of supplied
/// names against declared ones is quadratic. Bounding the supplied side is
/// what keeps the cost of refusing a nonsense call proportional to the schema
/// rather than to what was sent.
pub const MAX_SUPPLIED_ARGUMENTS: usize = 16;

/// How many bytes one text-carrying value may hold.
pub const MAX_ARGUMENT_VALUE_BYTES: usize = 4_096;

/// One value, already parsed into the type the caller believes it is.
///
/// Parsing happens at the boundary that received the model's reply; this
/// module decides whether what was parsed is what the tool declared. Keeping
/// the two apart is what makes "no coercion" a property rather than an
/// intention: by the time a value reaches here it has a type, and the check is
/// a comparison.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ArgumentValue {
    /// A number the task issued.
    ///
    /// Whether it still names anything is [`crate::handle::HandleTable`]'s
    /// answer, not this module's. Checking the shape and resolving the binding
    /// are two decisions, and a function that made both would be a function
    /// with two reasons to say no.
    Handle(u32),
    /// Text the model composed.
    Text(String),
    /// An address.
    Address(String),
    /// A non-negative count.
    Count(u64),
    /// Yes or no.
    Flag(bool),
    /// One name from a compiled-in set.
    Choice(String),
    /// A position in the values a person supplied for this task.
    ///
    /// The model never holds the value itself; see
    /// [`ParameterType::SuppliedValue`].
    SuppliedValue(u32),
}

impl ArgumentValue {
    /// A short, compiled-in name for the type, safe to record in an audit
    /// event.
    pub const fn type_label(&self) -> &'static str {
        match self {
            Self::Handle(_) => "handle",
            Self::Text(_) => "text",
            Self::Address(_) => "address",
            Self::Count(_) => "count",
            Self::Flag(_) => "flag",
            Self::Choice(_) => "choice",
            Self::SuppliedValue(_) => "supplied_value",
        }
    }

    /// Whether this value is of the declared type.
    ///
    /// A tag comparison and nothing more. There is no arm that accepts a
    /// neighbouring type because the value "would parse".
    pub const fn is_of_type(&self, declared: ParameterType) -> bool {
        match (self, declared) {
            (Self::Handle(_), ParameterType::Handle)
            | (Self::Text(_), ParameterType::Text)
            | (Self::Address(_), ParameterType::Address)
            | (Self::Count(_), ParameterType::Count)
            | (Self::Flag(_), ParameterType::Flag)
            | (Self::Choice(_), ParameterType::Choice(_))
            | (Self::SuppliedValue(_), ParameterType::SuppliedValue) => true,
            (
                Self::Handle(_)
                | Self::Text(_)
                | Self::Address(_)
                | Self::Count(_)
                | Self::Flag(_)
                | Self::Choice(_)
                | Self::SuppliedValue(_),
                _,
            ) => false,
        }
    }

    /// The bytes a text-carrying value holds, and nothing for the rest.
    pub fn text(&self) -> Option<&str> {
        match self {
            Self::Text(value) | Self::Address(value) | Self::Choice(value) => Some(value),
            // A supplied value carries a position, never bytes. That is the
            // whole point of the type: there is nothing here to return.
            Self::Handle(_) | Self::Count(_) | Self::Flag(_) | Self::SuppliedValue(_) => None,
        }
    }
}

/// One argument as it was supplied.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SuppliedArgument {
    /// The name the caller used.
    pub name: String,
    /// The value the caller sent.
    pub value: ArgumentValue,
}

impl SuppliedArgument {
    /// One supplied argument.
    pub fn new(name: impl Into<String>, value: ArgumentValue) -> Self {
        Self {
            name: name.into(),
            value,
        }
    }
}

/// Why a set of arguments was refused.
///
/// Closed, compiled-in, and ordered by where in the check the fault is found,
/// which is also the order [`validate`] applies them.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ArgumentRefusalReason {
    /// More arguments than [`MAX_SUPPLIED_ARGUMENTS`].
    TooManyArguments,
    /// The same name supplied twice. Refused rather than resolved by last-wins,
    /// which would be a coercion of the call itself.
    DuplicateArgument,
    /// A name the definition does not declare.
    UnknownArgument,
    /// A parameter marked required was not supplied.
    MissingRequiredArgument,
    /// The value is not of the declared type, and is never converted to it.
    TypeMismatch,
    /// A choice value outside the compiled-in set.
    NotAnOfferedChoice,
    /// A text-carrying value past [`MAX_ARGUMENT_VALUE_BYTES`].
    ValueTooLong,
    /// A text-carrying value with no bytes. A required parameter satisfied by
    /// an empty string is a missing argument wearing a name.
    EmptyValue,
}

impl ArgumentRefusalReason {
    /// Every reason, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::TooManyArguments,
        Self::DuplicateArgument,
        Self::UnknownArgument,
        Self::MissingRequiredArgument,
        Self::TypeMismatch,
        Self::NotAnOfferedChoice,
        Self::ValueTooLong,
        Self::EmptyValue,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::TooManyArguments => "too_many_arguments",
            Self::DuplicateArgument => "duplicate_argument",
            Self::UnknownArgument => "unknown_argument",
            Self::MissingRequiredArgument => "missing_required_argument",
            Self::TypeMismatch => "type_mismatch",
            Self::NotAnOfferedChoice => "not_an_offered_choice",
            Self::ValueTooLong => "value_too_long",
            Self::EmptyValue => "empty_value",
        }
    }
}

/// A named refusal, with the declared parameter it is about when there is one.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct ArgumentRefusal {
    /// Why.
    pub reason: ArgumentRefusalReason,
    /// The declared parameter at fault. `None` where the fault is about the
    /// call as a whole, or about a name only the caller used — see the module
    /// header on why the supplied name is not carried here.
    pub parameter: Option<&'static str>,
}

impl ArgumentRefusal {
    const fn about(reason: ArgumentRefusalReason, parameter: &'static str) -> Self {
        Self {
            reason,
            parameter: Some(parameter),
        }
    }

    const fn whole_call(reason: ArgumentRefusalReason) -> Self {
        Self {
            reason,
            parameter: None,
        }
    }
}

/// Decides whether `supplied` satisfies `definition`.
///
/// The order of the checks is fixed and part of the contract: the ceiling, then
/// duplicates, then unknown names, then each declared parameter in declaration
/// order. It matters because the answer feeds the repetition ledger
/// ([`super::RefusalLedger`]), which counts *identical* refusals — a check
/// order that varied would let the same wrong call be refused two different
/// ways and never be counted as a repeat.
pub fn validate(
    definition: ToolDefinition,
    supplied: &[SuppliedArgument],
) -> Result<(), ArgumentRefusal> {
    if supplied.len() > MAX_SUPPLIED_ARGUMENTS {
        return Err(ArgumentRefusal::whole_call(
            ArgumentRefusalReason::TooManyArguments,
        ));
    }
    for (index, argument) in supplied.iter().enumerate() {
        let repeated = supplied
            .iter()
            .skip(index.saturating_add(1))
            .any(|later| later.name == argument.name);
        if repeated {
            return Err(ArgumentRefusal {
                reason: ArgumentRefusalReason::DuplicateArgument,
                parameter: definition
                    .parameter(&argument.name)
                    .map(|parameter| parameter.name),
            });
        }
    }
    for argument in supplied {
        if !definition.declares(&argument.name) {
            return Err(ArgumentRefusal::whole_call(
                ArgumentRefusalReason::UnknownArgument,
            ));
        }
    }
    for parameter in definition.parameters {
        let found = supplied
            .iter()
            .find(|argument| argument.name == parameter.name);
        match found {
            None if parameter.required => {
                return Err(ArgumentRefusal::about(
                    ArgumentRefusalReason::MissingRequiredArgument,
                    parameter.name,
                ))
            }
            None => {}
            Some(argument) => check_value(*parameter, &argument.value)?,
        }
    }
    Ok(())
}

fn check_value(parameter: Parameter, value: &ArgumentValue) -> Result<(), ArgumentRefusal> {
    if !value.is_of_type(parameter.value_type) {
        return Err(ArgumentRefusal::about(
            ArgumentRefusalReason::TypeMismatch,
            parameter.name,
        ));
    }
    let Some(text) = value.text() else {
        return Ok(());
    };
    if text.is_empty() {
        return Err(ArgumentRefusal::about(
            ArgumentRefusalReason::EmptyValue,
            parameter.name,
        ));
    }
    if text.len() > MAX_ARGUMENT_VALUE_BYTES {
        return Err(ArgumentRefusal::about(
            ArgumentRefusalReason::ValueTooLong,
            parameter.name,
        ));
    }
    let offered = parameter.value_type.choices();
    if matches!(parameter.value_type, ParameterType::Choice(_)) && !offered.contains(&text) {
        return Err(ArgumentRefusal::about(
            ArgumentRefusalReason::NotAnOfferedChoice,
            parameter.name,
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{
        validate, ArgumentRefusalReason, ArgumentValue, SuppliedArgument, MAX_ARGUMENT_VALUE_BYTES,
        MAX_SUPPLIED_ARGUMENTS,
    };
    use crate::tool::definition::{Parameter, ParameterType, ToolDefinition};

    /// A fill's shape: a node to write into, and a position in what the
    /// person supplied (decision 0088).
    const FILL_PARAMETERS: &[Parameter] = &[
        Parameter::required("field", ParameterType::Handle, "The field to fill."),
        Parameter::required(
            "value_from",
            ParameterType::SuppliedValue,
            "Which of the person's supplied values to use.",
        ),
    ];

    fn fill_definition() -> ToolDefinition {
        ToolDefinition {
            name: "browser.form.fill",
            description: "Fill one field.",
            parameters: FILL_PARAMETERS,
        }
    }

    #[test]
    fn a_model_cannot_compose_a_value_where_a_persons_value_is_declared() {
        // The load-bearing guarantee of decision 0088 section 1: the model may
        // *name* a value the person supplied and may never *author* one. Text
        // supplied for a supplied-value parameter is refused rather than read
        // as the value, and rather than parsed into a position.
        for composed in [
            ArgumentValue::Text("123456".to_owned()),
            ArgumentValue::Text("the aadhaar number".to_owned()),
            // A count is a number the model chose. It must not stand in for a
            // reference to something only the person could have supplied.
            ArgumentValue::Count(0),
            ArgumentValue::Address("https://example.test".to_owned()),
        ] {
            let refusal = validate(
                fill_definition(),
                &[
                    SuppliedArgument::new("field", ArgumentValue::Handle(7)),
                    SuppliedArgument::new("value_from", composed.clone()),
                ],
            );
            assert_eq!(
                refusal.map_err(|refusal| refusal.reason),
                Err(ArgumentRefusalReason::TypeMismatch),
                "{}",
                composed.type_label()
            );
        }
    }

    #[test]
    fn naming_one_of_the_persons_values_is_accepted() {
        assert!(validate(
            fill_definition(),
            &[
                SuppliedArgument::new("field", ArgumentValue::Handle(7)),
                SuppliedArgument::new("value_from", ArgumentValue::SuppliedValue(2)),
            ],
        )
        .is_ok());
    }

    #[test]
    fn a_persons_value_cannot_stand_in_for_ordinary_text() {
        // The other direction, so the two types are not quietly interchangeable
        // at the point a fill is validated.
        const ASK_PARAMETERS: &[Parameter] = &[Parameter::required(
            "subject",
            ParameterType::Text,
            "What the task needs.",
        )];
        let refusal = validate(
            ToolDefinition {
                name: "user.ask",
                description: "Ask the person something.",
                parameters: ASK_PARAMETERS,
            },
            &[SuppliedArgument::new(
                "subject",
                ArgumentValue::SuppliedValue(0),
            )],
        );
        assert_eq!(
            refusal.map_err(|refusal| refusal.reason),
            Err(ArgumentRefusalReason::TypeMismatch)
        );
    }

    const DIRECTIONS: &[&str] = &["up", "down"];
    const PARAMETERS: &[Parameter] = &[
        Parameter::required(
            "direction",
            ParameterType::Choice(DIRECTIONS),
            "Which way to scroll.",
        ),
        Parameter::optional(
            "node",
            ParameterType::Handle,
            "The node to bring into view.",
        ),
        Parameter::optional("amount", ParameterType::Count, "How far."),
    ];

    fn definition() -> ToolDefinition {
        ToolDefinition {
            name: "browser.dom.scroll",
            description: "Low-risk semantic interaction",
            parameters: PARAMETERS,
        }
    }

    fn direction(value: &str) -> SuppliedArgument {
        SuppliedArgument::new("direction", ArgumentValue::Choice(value.to_owned()))
    }

    fn refusal(supplied: &[SuppliedArgument]) -> ArgumentRefusalReason {
        match validate(definition(), supplied) {
            Ok(()) => unreachable!("these arguments must be refused"),
            Err(refusal) => refusal.reason,
        }
    }

    #[test]
    fn a_call_carrying_exactly_what_is_declared_is_accepted() {
        assert_eq!(validate(definition(), &[direction("up")]), Ok(()));
        assert_eq!(
            validate(
                definition(),
                &[
                    direction("down"),
                    SuppliedArgument::new("node", ArgumentValue::Handle(7)),
                    SuppliedArgument::new("amount", ArgumentValue::Count(3)),
                ]
            ),
            Ok(())
        );
    }

    #[test]
    fn an_unknown_argument_name_fails_closed() {
        let supplied = vec![
            direction("up"),
            SuppliedArgument::new("selector", ArgumentValue::Text("h1".to_owned())),
        ];
        assert_eq!(refusal(&supplied), ArgumentRefusalReason::UnknownArgument);
    }

    #[test]
    fn a_refusal_never_carries_the_name_the_caller_invented() {
        // The one field through which model text could reach the journal, and
        // it is empty by construction rather than by redaction downstream.
        let supplied = vec![
            direction("up"),
            SuppliedArgument::new("selector", ArgumentValue::Text("h1".to_owned())),
        ];
        let Err(refused) = validate(definition(), &supplied) else {
            unreachable!("an undeclared name is refused")
        };
        assert_eq!(refused.parameter, None);
    }

    #[test]
    fn a_missing_required_argument_fails_closed() {
        let supplied = vec![SuppliedArgument::new("node", ArgumentValue::Handle(1))];
        let Err(refused) = validate(definition(), &supplied) else {
            unreachable!("the required choice is absent")
        };
        assert_eq!(
            refused.reason,
            ArgumentRefusalReason::MissingRequiredArgument
        );
        assert_eq!(refused.parameter, Some("direction"));
    }

    #[test]
    fn nothing_is_coerced_in_either_direction() {
        // A count written as text is not a count, and a count supplied where
        // text is declared is not text. Both directions, because a converter
        // usually only gets written for one of them and the other is the one
        // that surprises somebody.
        let as_text = vec![
            direction("up"),
            SuppliedArgument::new("amount", ArgumentValue::Text("3".to_owned())),
        ];
        assert_eq!(refusal(&as_text), ArgumentRefusalReason::TypeMismatch);
        let as_count = vec![SuppliedArgument::new("direction", ArgumentValue::Count(0))];
        assert_eq!(refusal(&as_count), ArgumentRefusalReason::TypeMismatch);
        let handle_as_count = vec![
            direction("up"),
            SuppliedArgument::new("node", ArgumentValue::Count(7)),
        ];
        assert_eq!(
            refusal(&handle_as_count),
            ArgumentRefusalReason::TypeMismatch
        );
    }

    #[test]
    fn a_choice_outside_the_compiled_in_set_is_refused_and_not_matched() {
        for value in ["left", "UP", "up ", "d"] {
            assert_eq!(
                refusal(&[direction(value)]),
                ArgumentRefusalReason::NotAnOfferedChoice,
                "{value}"
            );
        }
    }

    #[test]
    fn the_same_name_twice_is_refused_rather_than_resolved() {
        let supplied = vec![direction("up"), direction("down")];
        let Err(refused) = validate(definition(), &supplied) else {
            unreachable!("a repeated name is refused")
        };
        assert_eq!(refused.reason, ArgumentRefusalReason::DuplicateArgument);
        assert_eq!(refused.parameter, Some("direction"));
    }

    #[test]
    fn an_empty_text_value_is_a_missing_argument_wearing_a_name() {
        let supplied = vec![SuppliedArgument::new(
            "direction",
            ArgumentValue::Choice(String::new()),
        )];
        assert_eq!(refusal(&supplied), ArgumentRefusalReason::EmptyValue);
    }

    #[test]
    fn a_value_past_the_byte_bound_is_refused() {
        const TEXT: &[Parameter] = &[Parameter::required(
            "query",
            ParameterType::Text,
            "What to search for.",
        )];
        let definition = ToolDefinition {
            name: "browser.search",
            description: "Browser-owned search",
            parameters: TEXT,
        };
        let long = "x".repeat(MAX_ARGUMENT_VALUE_BYTES.saturating_add(1));
        let supplied = vec![SuppliedArgument::new("query", ArgumentValue::Text(long))];
        let Err(refused) = validate(definition, &supplied) else {
            unreachable!("a value past the bound is refused")
        };
        assert_eq!(refused.reason, ArgumentRefusalReason::ValueTooLong);
        let at_bound = "x".repeat(MAX_ARGUMENT_VALUE_BYTES);
        assert_eq!(
            validate(
                definition,
                &[SuppliedArgument::new(
                    "query",
                    ArgumentValue::Text(at_bound)
                )]
            ),
            Ok(())
        );
    }

    #[test]
    fn a_call_past_the_argument_ceiling_is_refused_before_anything_is_scanned() {
        let supplied: Vec<SuppliedArgument> = (0..=MAX_SUPPLIED_ARGUMENTS)
            .map(|index| SuppliedArgument::new(format!("a{index}"), ArgumentValue::Count(0)))
            .collect();
        assert_eq!(refusal(&supplied), ArgumentRefusalReason::TooManyArguments);
    }

    #[test]
    fn the_same_wrong_call_is_always_refused_the_same_way() {
        // The repetition ledger counts identical refusals, so a check order
        // that varied would let one wrong call wear two reasons and never look
        // like a repeat of itself.
        let supplied = vec![
            SuppliedArgument::new("selector", ArgumentValue::Text("h1".to_owned())),
            SuppliedArgument::new("amount", ArgumentValue::Text("3".to_owned())),
        ];
        let first = validate(definition(), &supplied);
        for _ in 0..4 {
            assert_eq!(validate(definition(), &supplied), first);
        }
    }

    #[test]
    fn every_reason_has_a_distinct_compiled_in_label() {
        let mut seen: Vec<&str> = Vec::new();
        for reason in ArgumentRefusalReason::ALL {
            assert!(!seen.contains(&reason.label()), "{}", reason.label());
            seen.push(reason.label());
        }
    }
}

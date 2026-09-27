// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a model is shown about one tool: a name, one line of description, and
//! a closed set of typed parameters.
//!
//! # The schema is a Rust structure, not a document
//!
//! Decision 0054 makes a tool a row compiled into the binary, and the argument
//! schema is part of that row. It is therefore [`Parameter`] values in a
//! `const` slice rather than a document the product parses at start-up. The
//! difference is not stylistic: a schema the product parses is a schema
//! something could hand it, and a tool name joins at dispatch to the
//! `ActionClass` that `policy-engine` reads. A parameter that could be
//! introduced is an input to an authority decision that could be introduced.
//!
//! There is also no pattern language here, and there will not be one. A
//! parameter is one of seven types, and where a value must come from a fixed set
//! the set is written out as [`ParameterType::Choice`]. Decision 0055 section 4
//! refuses stored expressions for procedures and section 5 refuses regular
//! expressions for page matching; a validating pattern in a tool schema would
//! be the same thing wearing a third noun.
//!
//! # A requirement here is unconditional
//!
//! [`Parameter::required`] does not depend on another parameter's value.
//! Expressing "required when `operation` is `close`" needs a language that can
//! read one field to decide about another, and that language is exactly what
//! the paragraph above refuses. A per-operation requirement is the executing
//! tool's own check, made where the operation is actually known, and this
//! schema deliberately admits the call so that the refusal comes from the
//! thing that understands it.

/// The type of one parameter.
///
/// Seven members, and the number is meant to stay small. Every additional type
/// is another shape a caller has to be able to produce, validate and record,
/// and the surface it buys is almost always expressible as a
/// [`Self::Choice`] over names the build already knows.
///
/// [`Self::SuppliedValue`] is the one that could not be. It exists because a
/// value a person typed is the one kind of argument the model must be able to
/// *name* and must never be able to *compose*, and no arrangement of the other
/// six says that: a choice over names is a vocabulary the model picks from, and
/// text is text.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ParameterType {
    /// A number this task issued for one thing it showed the model.
    ///
    /// Node handles come from [`crate::handle::HandleTable`], which issues a
    /// number once, binds it to one node, and never reuses it. A tab listing
    /// uses the same bounded wire shape but a distinct task-scoped resolver;
    /// a tab number must never be looked up in the DOM handle table.
    Handle,
    /// Bounded text the model composes.
    Text,
    /// An address to open or fetch.
    ///
    /// Bounded and non-empty, and nothing more. This module does not parse an
    /// address, resolve it, or decide anything about its origin — origin is a
    /// `policy-engine` decision, per action, and a shape check here that looked
    /// like one would be a second place to get it wrong.
    Address,
    /// A non-negative count.
    Count,
    /// Yes or no.
    Flag,
    /// Exactly one of a compiled-in set of names.
    ///
    /// The set is the whole vocabulary for that parameter. A value outside it
    /// is refused rather than matched to the nearest member, which is the same
    /// rule every closed enumeration in this product follows.
    Choice(&'static [&'static str]),
    /// A position in the values a person supplied for this task.
    ///
    /// This is how a tool takes a value the model must never hold. The model
    /// asked for the values with `user.request_values`, the person typed them
    /// into a surface the browser owns, and what came back was a list of
    /// positions. Naming one here says "use the third thing they gave you"
    /// and cannot say anything else: the type carries an index, so there is no
    /// argument shape in which a composed value fits (decisions 0063 and 0088).
    ///
    /// It is deliberately not [`Self::Count`], although both are numbers. A
    /// count is a quantity the model chose; this is a reference to something
    /// only the person could supply, and the two must not be interchangeable
    /// at the point a fill is validated.
    SuppliedValue,
}

impl ParameterType {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Handle => "handle",
            Self::Text => "text",
            Self::Address => "address",
            Self::Count => "count",
            Self::Flag => "flag",
            Self::Choice(_) => "choice",
            Self::SuppliedValue => "supplied_value",
        }
    }

    /// The names a [`Self::Choice`] admits, and nothing for every other type.
    pub const fn choices(self) -> &'static [&'static str] {
        match self {
            Self::Choice(names) => names,
            Self::Handle
            | Self::Text
            | Self::Address
            | Self::Count
            | Self::Flag
            | Self::SuppliedValue => &[],
        }
    }

    /// Whether a value of this type carries bytes a caller supplied.
    ///
    /// The three that do are the three that need a length bound and a rule
    /// against emptiness; the three that do not are already bounded by their
    /// own Rust type.
    pub const fn carries_text(self) -> bool {
        matches!(self, Self::Text | Self::Address | Self::Choice(_))
    }
}

/// One parameter of one tool.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Parameter {
    /// The argument name, lowercase and underscored.
    pub name: &'static str,
    /// What kind of value it takes.
    pub value_type: ParameterType,
    /// Whether a call without it is refused. Unconditional; see the module
    /// header.
    pub required: bool,
    /// One line, for the model.
    pub description: &'static str,
}

impl Parameter {
    /// A parameter a call must carry.
    pub const fn required(
        name: &'static str,
        value_type: ParameterType,
        description: &'static str,
    ) -> Self {
        Self {
            name,
            value_type,
            required: true,
            description,
        }
    }

    /// A parameter a call may omit.
    pub const fn optional(
        name: &'static str,
        value_type: ParameterType,
        description: &'static str,
    ) -> Self {
        Self {
            name,
            value_type,
            required: false,
            description,
        }
    }
}

/// The whole of what a model is told about one tool.
///
/// A projection of the registry row rather than a second copy of it:
/// [`crate::tool::ToolEntry::definition`] builds it, so the name and the
/// description have exactly one source. What is deliberately *not* here is the
/// milestone, the idempotency class and the way the name is matched. Those are
/// consequence-bearing attributes the product reads about a call; showing them
/// to a model would invite it to argue about them, and decision 0054 section 1
/// is that they come from the row and never from the proposal.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToolDefinition {
    /// The internal dotted name.
    pub name: &'static str,
    /// One line saying what the tool is for.
    pub description: &'static str,
    /// Every parameter it takes, in the order they are declared.
    pub parameters: &'static [Parameter],
}

impl ToolDefinition {
    /// The parameter called `name`, when one is declared.
    pub fn parameter(self, name: &str) -> Option<Parameter> {
        self.parameters
            .iter()
            .copied()
            .find(|parameter| parameter.name == name)
    }

    /// Whether `name` is a declared parameter.
    pub fn declares(self, name: &str) -> bool {
        self.parameter(name).is_some()
    }

    /// Every parameter a call must carry, in declaration order.
    pub fn required_parameters(self) -> impl Iterator<Item = Parameter> {
        self.parameters
            .iter()
            .copied()
            .filter(|parameter| parameter.required)
    }

    /// Whether the definition is one this module's rules admit.
    ///
    /// Checked by a test over the whole registry rather than at run time: the
    /// table is `const`, so a malformed row is a build-time fact and a run-time
    /// check for it would be a check that can never fire in a shipped product.
    pub fn is_well_formed(self) -> bool {
        if self.name.is_empty() || self.description.is_empty() {
            return false;
        }
        let mut seen: Vec<&str> = Vec::new();
        for parameter in self.parameters {
            if seen.contains(&parameter.name) || !parameter_name_is_plain(parameter.name) {
                return false;
            }
            if parameter.description.is_empty() {
                return false;
            }
            if !choices_are_plain_and_distinct(parameter.value_type) {
                return false;
            }
            seen.push(parameter.name);
        }
        true
    }
}

fn parameter_name_is_plain(name: &str) -> bool {
    !name.is_empty()
        && name
            .chars()
            .all(|character| character.is_ascii_lowercase() || character == '_')
}

fn choices_are_plain_and_distinct(value_type: ParameterType) -> bool {
    let names = value_type.choices();
    if matches!(value_type, ParameterType::Choice(_)) && names.is_empty() {
        // A choice over nothing admits nothing, so every call naming it is
        // refused and the parameter is a tool that cannot be used. That is a
        // defect rather than a very strict schema, and it fails here.
        return false;
    }
    let mut seen: Vec<&str> = Vec::new();
    for name in names {
        if seen.contains(name) || !parameter_name_is_plain(name) {
            return false;
        }
        seen.push(name);
    }
    true
}

#[cfg(test)]
mod tests {
    use super::{Parameter, ParameterType, ToolDefinition};

    const DIRECTIONS: &[&str] = &["up", "down"];

    fn definition() -> ToolDefinition {
        const PARAMETERS: &[Parameter] = &[
            Parameter::required("node", ParameterType::Handle, "The node to scroll to."),
            Parameter::optional(
                "direction",
                ParameterType::Choice(DIRECTIONS),
                "Which way to scroll.",
            ),
        ];
        ToolDefinition {
            name: "browser.dom.scroll",
            description: "Low-risk semantic interaction",
            parameters: PARAMETERS,
        }
    }

    #[test]
    fn a_definition_answers_about_the_parameters_it_declares() {
        let definition = definition();
        assert!(definition.declares("node"));
        assert!(!definition.declares("selector"));
        assert_eq!(
            definition.parameter("direction").map(|it| it.value_type),
            Some(ParameterType::Choice(DIRECTIONS))
        );
        let required: Vec<&str> = definition
            .required_parameters()
            .map(|parameter| parameter.name)
            .collect();
        assert_eq!(required, vec!["node"]);
    }

    #[test]
    fn only_a_choice_offers_names() {
        for value_type in [
            ParameterType::Handle,
            ParameterType::Text,
            ParameterType::Address,
            ParameterType::Count,
            ParameterType::Flag,
        ] {
            assert!(value_type.choices().is_empty(), "{}", value_type.label());
        }
        assert_eq!(ParameterType::Choice(DIRECTIONS).choices(), DIRECTIONS);
    }

    #[test]
    fn exactly_the_three_text_carrying_types_say_so() {
        for value_type in [
            ParameterType::Text,
            ParameterType::Address,
            ParameterType::Choice(DIRECTIONS),
        ] {
            assert!(value_type.carries_text(), "{}", value_type.label());
        }
        for value_type in [
            ParameterType::Handle,
            ParameterType::Count,
            ParameterType::Flag,
        ] {
            assert!(!value_type.carries_text(), "{}", value_type.label());
        }
    }

    #[test]
    fn a_well_formed_definition_passes_its_own_check() {
        assert!(definition().is_well_formed());
    }

    #[test]
    fn a_malformed_definition_is_named_by_the_check_rather_than_shipped() {
        const DUPLICATED: &[Parameter] = &[
            Parameter::required("node", ParameterType::Handle, "One."),
            Parameter::optional("node", ParameterType::Count, "The same name again."),
        ];
        const SHOUTED: &[Parameter] = &[Parameter::required(
            "Node",
            ParameterType::Handle,
            "A name that is not lowercase.",
        )];
        const UNDESCRIBED: &[Parameter] = &[Parameter::required("node", ParameterType::Handle, "")];
        // A choice over nothing is the one that reads as strictness and is a
        // defect: every call naming the parameter would be refused.
        const EMPTY_CHOICE: &[Parameter] = &[Parameter::required(
            "direction",
            ParameterType::Choice(&[]),
            "Which way.",
        )];
        for parameters in [DUPLICATED, SHOUTED, UNDESCRIBED, EMPTY_CHOICE] {
            let candidate = ToolDefinition {
                name: "browser.dom.scroll",
                description: "Low-risk semantic interaction",
                parameters,
            };
            assert!(!candidate.is_well_formed());
        }
        for (name, description) in [("", "Something"), ("browser.dom.scroll", "")] {
            let candidate = ToolDefinition {
                name,
                description,
                parameters: &[],
            };
            assert!(!candidate.is_well_formed());
        }
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/// The structural reason a comma-separated document was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CsvErrorKind {
    Empty,
    LoneLineBreak,
    QuoteInUnquotedField,
    BytesAfterClosingQuote,
    UnterminatedQuotedField,
    EmptyHeader,
    DuplicateHeader,
    RaggedRow,
    NulByte,
    DisallowedControl,
}

/// A closed refusal from table parsing or transformation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InputTooLarge,
    RecipeTooLarge,
    OutputTooLarge,
    InvalidUtf8,
    InvalidCsv(CsvErrorKind),
    InvalidRecipe,
    TooManyRows,
    TooManyColumns,
    TooManyCells,
    CellTooLarge,
    TooManySteps,
    UnknownColumn,
    DuplicateColumn,
    InvalidDecimal,
    DecimalOverflow,
    LineageTooLarge,
    Cancelled,
}

impl Error {
    /// A stable, content-free label suitable for a transcript or audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::InputTooLarge => "input_too_large",
            Self::RecipeTooLarge => "recipe_too_large",
            Self::OutputTooLarge => "output_too_large",
            Self::InvalidUtf8 => "invalid_utf8",
            Self::InvalidCsv(_) => "invalid_csv",
            Self::InvalidRecipe => "invalid_recipe",
            Self::TooManyRows => "too_many_rows",
            Self::TooManyColumns => "too_many_columns",
            Self::TooManyCells => "too_many_cells",
            Self::CellTooLarge => "cell_too_large",
            Self::TooManySteps => "too_many_steps",
            Self::UnknownColumn => "unknown_column",
            Self::DuplicateColumn => "duplicate_column",
            Self::InvalidDecimal => "invalid_decimal",
            Self::DecimalOverflow => "decimal_overflow",
            Self::LineageTooLarge => "lineage_too_large",
            Self::Cancelled => "cancelled",
        }
    }
}

impl core::fmt::Display for Error {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter.write_str(self.label())
    }
}

impl std::error::Error for Error {}

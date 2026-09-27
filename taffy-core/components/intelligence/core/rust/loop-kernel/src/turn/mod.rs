// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One model turn: composed from durable facts, read back into a residency.

pub mod model_turn;
pub mod person_answer;

pub use model_turn::{
    compose_configured_model_turn, compose_model_turn, managed_request_id, model_request_id,
    read_model_reply, read_model_stream_terminal, ComposedModelTurn, ModelReplyReading,
    ModelReplyStream, ModelResponseStyle, ModelTurnError, ReplyWire,
};
pub use person_answer::{
    ask_subject_from_residency, classify_ask_subject, classify_follow_up_question,
    classify_person_answer, PersonAnswerRefusal, MAX_PERSON_ANSWER_BYTES,
};

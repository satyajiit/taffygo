// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * What one probe of an address answered (decision
 * `docs/decisions/0096-an-endpoint-a-person-typed-is-registered-with-the-browser.md`
 * section 5).
 *
 * The probe proves an **address**, not a credential. Decision 0083's probe
 * asks whether a key works; this one asks what is there, reading the answer
 * from the server's own response rather than guessing from a port. So the two
 * results are different vocabularies and are deliberately not one type: a
 * verdict about a key says nothing about an address, and the reverse.
 *
 * [Reached] with no models at all is an answer and is drawn as one — the
 * address is right and nothing is loaded behind it — never as a failure.
 */
sealed interface CustomEndpointOutcome {

    /**
     * Whether the server answered that it will say nothing without a
     * credential.
     *
     * Asked of the whole answer rather than read off [Refused] at each place
     * that cares, because it is the one verdict that names something a person
     * can supply on this page, and the page opens the field for it.
     */
    val wantsACredential: Boolean
        get() = this is Refused && problem == Problem.WANTS_A_CREDENTIAL

    /** The address answered, and said what it is running. */
    data class Reached(
        /** The runtime the server named itself as. */
        val server: ServerKind,
        /**
         * How many models the server said it has. Zero is an answer.
         *
         * The count the **server** named, which is not [models]`.size`: the
         * list is what survived the contract's bound and the count is what was
         * offered. Reading the list's length as the count is the silent
         * truncation decisions 0096 section 5 and 0098 section 4 both refuse —
         * it would tell somebody their server offers thirty-two models when it
         * offered fifty.
         */
        val modelCount: Int,
        /**
         * The models the probe actually read, up to the contract's bound.
         *
         * Whole specs rather than names: the window, the output allowance and
         * both capability flags come from the server's own answer, so a save
         * files what is really there instead of a placeholder claiming nothing.
         */
        val models: List<Model>,
        /**
         * The base the OpenAI-shaped API was proved at, null when nothing was
         * proved.
         *
         * The transport joins only the operation beneath a person's base, so
         * `http://box:11434/v1` routes and a bare `http://box:11434` routes to
         * `/chat/completions`, which no runtime serves — while the prober's
         * native fallbacks answer on that bare origin and would otherwise have
         * reported it as working. This is which of the two happened, said by
         * the only party that knows. Absent means nothing was proved, and is
         * rendered as that rather than as a value.
         */
        val provedBase: String?,
    ) : CustomEndpointOutcome {
        /** Whether the server has anything loaded to route to. */
        val carriesModels: Boolean get() = modelCount > 0

        /**
         * Whether the server listed more than the contract carries, so what is
         * about to be saved is part of what is there rather than all of it.
         */
        val truncated: Boolean get() = models.size < modelCount
    }

    /**
     * One model a server listed, as the probe read it.
     *
     * The feature's own vocabulary rather than the contract's, for the reason
     * [CustomEndpoints] gives: a surface that named a generated wire type would
     * be edited every time the wire moved, and a host test of this screen would
     * have to build one.
     */
    data class Model(
        /** The identity the server uses, which is also what a person knows it by. */
        val modelId: String,
        /** What to show. The server's own name for it when it gave one. */
        val displayName: String,
        /** How much the model can be given, as the server reported it. */
        val contextWindow: UInt,
        /** How much it will produce, as the server reported it. */
        val maxOutputTokens: UInt,
        /** Whether it reasons before answering. */
        val reasoning: Boolean,
        /** Whether it can call tools. */
        val toolCalling: Boolean,
    )

    /** The address did not answer the question, in one of four ways. */
    data class Refused(val problem: Problem) : CustomEndpointOutcome

    /**
     * The runtimes a probe can name.
     *
     * Held equal to the Core API's `ServerKindView` member for member. It is
     * the server's own answer about itself rather than a guess from a port
     * number, which is why a person can point this at a port nobody uses and
     * still be told what is there.
     */
    enum class ServerKind {
        /** The OpenAI-shaped model listing answered at the address as typed. */
        OPENAI_COMPATIBLE,
        OLLAMA,
        LM_STUDIO,
        VLLM,
        LLAMA_CPP,
    }

    /**
     * The four ways an address can fail to answer, each a different thing for
     * a person to do about it.
     *
     * Four rather than one, because "nothing is listening there" and "it is
     * listening and wants a key" are not the same problem and do not have the
     * same fix. And four rather than nine, because the probe's transport
     * vocabulary is about credentials — the members that separate a refused
     * key from an unpaid account say nothing about whether an address is the
     * right one, so they fold into the one thing they do say here: something
     * answered, and TaffyGo could not read it as a model server.
     */
    enum class Problem {
        /** Nothing answered at that address at all. */
        NOTHING_ANSWERED,

        /** Something answered, and it was not a model listing TaffyGo can read. */
        NOT_A_MODEL_SERVER,

        /** Something answered, and it will not say anything without a credential. */
        WANTS_A_CREDENTIAL,

        /**
         * No answer came back. The check timed out, or could not be run at
         * all — and in both cases nothing was learned about the address, which
         * is the only thing this screen can honestly say about either.
         */
        NO_ANSWER,
    }
}

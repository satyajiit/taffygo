// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import java.net.URI
import java.net.URISyntaxException
import taffy.core_api.MAX_PROVIDER_ENDPOINT_BYTES

/**
 * What can be said about an address on screen SCR-418 without calling anybody.
 *
 * ## This is not the gate
 *
 * The browser owns the refusal (decision
 * `docs/decisions/0096-an-endpoint-a-person-typed-is-registered-with-the-browser.md`
 * section 3), and it owns it with the network stack's own parser: it
 * canonicalizes the host before deciding, so `http://0x7f.1` and
 * `http://2130706433` are both loopback by the time it looks. Nothing here can
 * do that. So this is the same rule read early, to save a person a round trip
 * — exactly what the key form's prefix hint is for on screen SCR-415 — and
 * where it cannot tell, it **admits** and lets the browser answer. A screen
 * that refused more than the product does would tell somebody to fix an
 * address that would have worked.
 *
 * ## Propose, never rewrite
 *
 * The address a person typed is the address that is registered, byte for byte
 * (section 1), because the question the register asks is "did this person type
 * this?" and that question has exactly one right answer. Nothing here edits
 * what was typed, and nothing here works out a better address either: the base
 * the model API was actually proved at comes back on the probe's own verdict,
 * and `CustomEndpointProjection.proposalFor` turns it into something the screen
 * offers and a person accepts.
 */
object CustomEndpointAddress {

    /**
     * Why an address may not be registered, in the browser's own categories.
     *
     * One member per `CustomEndpointRefusal` in
     * `taffy-core/browser/model/custom_provider_endpoint_policy.h`, in that
     * file's order, so the sentence a person reads here and the refusal they
     * would have got are the same thing said once.
     */
    enum class Refusal {
        /** Past the contract's bound on an endpoint. */
        TOO_LONG,

        /** Nothing parses out of it, it names no host, or the scheme is neither https nor http. */
        NOT_AN_ADDRESS,

        /** Plain http to something that is not a literal local address. */
        CLEARTEXT_NOT_LOCAL,

        /** A `user:password@` prefix. */
        CARRIES_CREDENTIALS,

        /** A `?query`. */
        CARRIES_QUERY,

        /** A `#fragment`. */
        CARRIES_FRAGMENT,
    }

    /**
     * The addresses the common runtimes serve on, each **with its version
     * segment**.
     *
     * The segment is not decoration. The transport joins only the operation
     * beneath what a person typed, so `…:11434/v1` becomes
     * `…/v1/chat/completions` and a bare `…:11434` becomes
     * `…/chat/completions`, which no runtime serves. A preset that dropped it
     * would hand somebody an address that probes green and fails on every
     * request.
     *
     * `localhost` because that is what a person running the server on the
     * machine in front of them types, and because RFC 6761 makes it the one
     * name the address policy can admit as a literal. A server on another
     * machine is the same address with that host replaced.
     */
    enum class Preset(val address: String) {
        OLLAMA("http://localhost:11434/v1"),
        LM_STUDIO("http://localhost:1234/v1"),
        LLAMA_CPP("http://localhost:8080/v1"),
        VLLM("http://localhost:8000/v1"),
    }

    /**
     * Why this address would be refused, or null when nothing here can see a
     * reason.
     *
     * The order is the browser's, and the order is the point: somebody who
     * typed a query onto their endpoint has a different thing to fix than
     * somebody who typed http, and naming the scheme first would send them to
     * fix the wrong one.
     */
    fun classify(typed: String): Refusal? {
        val value = typed.trim()
        if (value.isEmpty()) return Refusal.NOT_AN_ADDRESS
        if (exceedsUtf8Bytes(value, MAX_PROVIDER_ENDPOINT_BYTES)) return Refusal.TOO_LONG
        val url = parse(value) ?: return Refusal.NOT_AN_ADDRESS
        if (url.scheme != HTTPS && url.scheme != HTTP) return Refusal.NOT_AN_ADDRESS
        if (url.host.isNullOrEmpty()) return Refusal.NOT_AN_ADDRESS
        if (url.userInfo != null) return Refusal.CARRIES_CREDENTIALS
        if (url.query != null) return Refusal.CARRIES_QUERY
        if (url.fragment != null) return Refusal.CARRIES_FRAGMENT
        if (url.scheme == HTTP && !namesALiteralLocalAddress(url.host)) {
            return Refusal.CLEARTEXT_NOT_LOCAL
        }
        return null
    }

    /**
     * Whether [value] weighs more than [maximum] bytes on the UTF-8 wire.
     *
     * SCR-418 calls [classify] on each edit. Encoding the whole field merely
     * to ask for its size allocated a new byte array for every keystroke and
     * kept scanning after the answer was already known. This count allocates
     * nothing and stops at the first byte beyond the contract ceiling.
     */
    private fun exceedsUtf8Bytes(value: String, maximum: Int): Boolean {
        var bytes = 0
        var index = 0
        while (index < value.length) {
            val code = value[index].code
            bytes += when {
                code < 0x80 -> 1
                code < 0x800 -> 2
                value[index].isHighSurrogate() &&
                    index + 1 < value.length &&
                    value[index + 1].isLowSurrogate() -> {
                    index += 1
                    4
                }
                else -> 3
            }
            if (bytes > maximum) return true
            index += 1
        }
        return false
    }

    /**
     * The machine this address names, or null when nothing parses out of it.
     *
     * Read for one purpose: seeding the identity a provider is filed under
     * before a person has said what to call it. The check comes before the name
     * field on this page, and the probe has to name the identity its verdict
     * will be filed under, so something has to stand in — and the machine they
     * pointed at is the one thing they have already said.
     */
    fun hostOf(typed: String): String? = parse(typed.trim())?.host?.takeIf { it.isNotEmpty() }

    /**
     * Whether this address is plain http to a machine on the person's own
     * network.
     *
     * Allowed, and still worth saying out loud: what leaves the phone for this
     * address leaves it unencrypted, and the only thing standing between it
     * and anybody else on that network is the network.
     */
    fun isCleartextToLocal(typed: String): Boolean {
        val url = parse(typed.trim()) ?: return false
        // `URI` parses `http://` and `http:/` without complaint and reports no
        // host for either, and this runs on every keystroke, so those are two
        // of the states a person types *through* on the way to an address
        // rather than odd input. An address with no host names no machine, so
        // it is not a local one; reading the platform type straight into the
        // non-null parameter below threw instead, and took the browser process
        // with it. `hostOf` directly above has always treated a missing host
        // as no host.
        val host = url.host ?: return false
        return url.scheme == HTTP && namesALiteralLocalAddress(host)
    }

    /**
     * Whether the address is a literal local one, in the sense decision 0096
     * section 3 means it.
     *
     * Deliberately narrower than the browser's answer in two places, and both
     * are safe in the direction that matters. It reads only the first hextet
     * of an IPv6 literal, so an uncompressed `0:0:0:0:0:0:0:1` reads as
     * non-local; and it does not canonicalize a host, so an IPv4 address
     * written in hex or as a single integer reads as a name. In both cases the
     * screen asks for https where the browser would have taken http — a
     * nuisance for an address nobody types, rather than an address this screen
     * waved through.
     */
    private fun namesALiteralLocalAddress(host: String): Boolean {
        val bare = host.removePrefix("[").removeSuffix("]").substringBefore('%').lowercase()
        if (bare.isEmpty()) return false
        ipv4Octets(bare)?.let { return isLocalIpv4(it) && !isMetadataIpv4(it) }
        if (bare.contains(':')) return isLocalIpv6(bare)
        // `localhost` is admitted with the literals rather than with the names
        // it resembles: RFC 6761 reserves it, resolvers answer it from the
        // loopback interface without asking the network, and no party owns it
        // to repoint — so the one reason this rule refuses names cannot apply
        // to it. `.local` is the one name form the record does admit.
        return bare == LOCALHOST || bare.endsWith(".$LOCALHOST") || bare.endsWith(MDNS_SUFFIX)
    }

    /** The four octets, or null when this is not a dotted-quad literal. */
    private fun ipv4Octets(host: String): List<Int>? {
        val parts = host.split('.')
        if (parts.size != IPV4_OCTETS) return null
        val octets = parts.map { part ->
            if (part.isEmpty() || part.length > 3 || !part.all(Char::isDigit)) return null
            part.toInt().takeIf { it <= OCTET_MAX } ?: return null
        }
        return octets
    }

    /** Loopback, the three private ranges, and link-local. */
    private fun isLocalIpv4(octets: List<Int>): Boolean {
        val first = octets[0]
        val second = octets[1]
        return when {
            first == LOOPBACK_PREFIX -> true
            first == 10 -> true
            first == 172 && second in 16..31 -> true
            first == 192 && second == 168 -> true
            first == 169 && second == 254 -> true
            else -> false
        }
    }

    /**
     * The cloud instance-metadata address, refused although it sits inside a
     * range this rule admits. Nobody runs a model server on it, so refusing it
     * costs a person nothing and takes the most-attempted request-forgery
     * destination off the list of addresses this product can be talked into
     * holding.
     */
    private fun isMetadataIpv4(octets: List<Int>): Boolean =
        octets == listOf(169, 254, 169, 254)

    /**
     * `::1`, `fc00::/7` and `fe80::/10`, plus the IPv4-mapped form.
     *
     * `fc00::/7` is what RFC1918 is in IPv6, and a person whose home network
     * is v6-only reaches their own machine at one of these; leaving it out
     * would admit a laptop at `192.168.1.9` and refuse the same laptop at its
     * unique-local address, which is a distinction about their router rather
     * than about who receives their data.
     */
    private fun isLocalIpv6(host: String): Boolean {
        if (host == IPV6_LOOPBACK) return true
        if (host == METADATA_IPV6) return false
        if (host.startsWith(IPV4_MAPPED_PREFIX)) {
            val mapped = ipv4Octets(host.removePrefix(IPV4_MAPPED_PREFIX)) ?: return false
            return isLocalIpv4(mapped) && !isMetadataIpv4(mapped)
        }
        val hextet = host.substringBefore(':').takeIf { it.isNotEmpty() } ?: return false
        val value = hextet.toIntOrNull(HEX) ?: return false
        val leadingByte = value shr Byte.SIZE_BITS
        return leadingByte in UNIQUE_LOCAL_BYTES || value in LINK_LOCAL_RANGE
    }

    /**
     * The address as a URI, or null when it is not one.
     *
     * `java.net.URI` rather than a pattern, for the reason the browser's own
     * policy file gives about patterns: they are the part that gets it wrong.
     * It is stricter than the network stack about a few hosts, which is the
     * admitted narrowness above.
     */
    private fun parse(value: String): URI? = try {
        URI(value)
    } catch (_: URISyntaxException) {
        null
    }

    private const val HTTP = "http"
    private const val HTTPS = "https"
    private const val LOCALHOST = "localhost"
    private const val MDNS_SUFFIX = ".local"
    private const val IPV6_LOOPBACK = "::1"
    private const val METADATA_IPV6 = "fd00:ec2::254"
    private const val IPV4_MAPPED_PREFIX = "::ffff:"
    private const val IPV4_OCTETS = 4
    private const val OCTET_MAX = 255
    private const val LOOPBACK_PREFIX = 127
    private const val HEX = 16
    private val UNIQUE_LOCAL_BYTES = 0xfc..0xfd
    private val LINK_LOCAL_RANGE = 0xfe80..0xfebf
}

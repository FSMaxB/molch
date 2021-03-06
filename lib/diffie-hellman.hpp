/*
 * Molch, an implementation of the axolotl ratchet based on libsodium
 *
 * ISC License
 *
 * Copyright (C) 2015-2016 1984not Security GmbH
 * Author: Max Bruckner (FSMaxB)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef LIB_DIFFIE_HELLMAN_H
#define LIB_DIFFIE_HELLMAN_H

#include "buffer.hpp"
#include "ratchet.hpp"

namespace Molch {
	/*
	 * Diffie Hellman key exchange using our private key and the
	 * other's public key. Our public key is used to derive a Hash
	 * from the actual output of the diffie hellman exchange (see
	 * documentation of libsodium).
	 *
	 * role: specifies if I am Alice or Bob. This determines in
	 * what order the public keys get hashed.
	 *
	 * OUTPUT:
	 * Alice: H(ECDH(our_private_key,their_public_key)|our_public_key|their_public_key)
	 * Bob:   H(ECDH(our_private_key,their_public_key)|their_public_key|our_public_key)
	 */
	result<Key<DIFFIE_HELLMAN_SIZE,KeyType::Key>> diffie_hellman(
			const PrivateKey& our_private_key, //needs to be PRIVATE_KEY_SIZE long
			const PublicKey& our_public_key, //needs to be PUBLIC_KEY_SIZE long
			const PublicKey& their_public_key, //needs to be PUBLIC_KEY_SIZE long
			const Ratchet::Role role);

	/*
	 * Triple Diffie Hellman with two keys.
	 *
	 * role: specifies if I am Alice or Bob. This determines in
	 * what order the public keys get hashed.
	 *
	 * OUTPUT:
	 * HASH(DH(A,B0) || DH(A0,B) || DH(A0,B0))
	 * Where:
	 * A: Alice's identity
	 * A0: Alice's ephemeral
	 * B: Bob's identity
	 * B0: Bob's ephemeral
	 * -->Alice: HASH(DH(our_identity, their_ephemeral)||DH(our_ephemeral, their_identity)||DH(our_ephemeral, their_ephemeral))
	 * -->Bob: HASH(DH(their_identity, our_ephemeral)||DH(our_identity, their_ephemeral)||DH(our_ephemeral, their_ephemeral))
	 */
	result<EmptyableKey<DIFFIE_HELLMAN_SIZE,KeyType::Key>> triple_diffie_hellman(
			const PrivateKey& our_private_identity,
			const PublicKey& our_public_identity,
			const PrivateKey& our_private_ephemeral,
			const PublicKey& our_public_ephemeral,
			const PublicKey& their_public_identity,
			const PublicKey& their_public_ephemeral,
			const Ratchet::Role role);
}

#endif

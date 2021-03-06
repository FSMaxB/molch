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

#include <cstdio>
#include <cstdlib>
#include <sodium.h>
#include <exception>
#include <iostream>

#include "../lib/key-derivation.hpp"
#include "molch/constants.h"
#include "utils.hpp"
#include "common.hpp"
#include "exception.hpp"

using namespace Molch;

int main() {
	try {
		TRY_VOID(Molch::sodium_init());

		//create Alice's identity keypair
		PublicKey alice_public_identity;
		PrivateKey alice_private_identity;
		generate_and_print_keypair(
			alice_public_identity,
			alice_private_identity,
			"Alice",
			"identity");

		//create Alice's ephemeral keypair
		PublicKey alice_public_ephemeral;
		PrivateKey alice_private_ephemeral;
		generate_and_print_keypair(
			alice_public_ephemeral,
			alice_private_ephemeral,
			"Alice",
			"ephemeral");

		//create Bob's identity keypair
		PublicKey bob_public_identity;
		PrivateKey bob_private_identity;
		generate_and_print_keypair(
			bob_public_identity,
			bob_private_identity,
			"Bob",
			"identity");

		//create Bob's ephemeral keypair
		PublicKey bob_public_ephemeral;
		PrivateKey bob_private_ephemeral;
		generate_and_print_keypair(
			bob_public_ephemeral,
			bob_private_ephemeral,
			"Bob",
			"ephemeral");

		//derive Alice's initial root and chain key
		TRY_WITH_RESULT(alice_derived_keys_result, derive_initial_root_chain_and_header_keys(
			alice_private_identity,
			alice_public_identity,
			bob_public_identity,
			alice_private_ephemeral,
			alice_public_ephemeral,
			bob_public_ephemeral,
			Ratchet::Role::ALICE));
		const auto& alice_derived_keys{alice_derived_keys_result.value()};

		//print Alice's initial root and chain key
		std::cout << "Alice's initial root key:\n";
		std::cout << alice_derived_keys.root_key << std::endl;
		if (alice_derived_keys.send_chain_key.has_value()) {
			throw Exception(status_type::INCORRECT_DATA, "Alice should not have a send chain key.");
		}
		std::cout << "Alice's initial receive chain key:\n";
		std::cout << alice_derived_keys.receive_chain_key.value() << std::endl;
		if (alice_derived_keys.send_header_key.has_value()) {
			throw Exception(status_type::INCORRECT_DATA, "Alice should not have a send header key.");
		}
		std::cout << "Alice's initial receive header key:n";
		std::cout << alice_derived_keys.receive_header_key.value() << std::endl;
		std::cout << "Alice's initial next send header key:\n";
		std::cout << alice_derived_keys.next_send_header_key << std::endl;
		std::cout << "Alice's initial next receive header key\n";
		std::cout << alice_derived_keys.next_receive_header_key << std::endl;

		//derive Bob's initial root and chain key
		TRY_WITH_RESULT(bob_derived_keys_result, derive_initial_root_chain_and_header_keys(
			bob_private_identity,
			bob_public_identity,
			alice_public_identity,
			bob_private_ephemeral,
			bob_public_ephemeral,
			alice_public_ephemeral,
			Ratchet::Role::BOB));
		const auto& bob_derived_keys{bob_derived_keys_result.value()};

		//print Bob's initial root and chain key
		std::cout << "Bob's initial root key:\n";
		std::cout << bob_derived_keys.root_key << std::endl;
		std::cout << "Bob's initial send chain key:\n";
		std::cout << bob_derived_keys.send_chain_key.value() << std::endl;
		if (bob_derived_keys.receive_chain_key.has_value()) {
			throw Exception(status_type::INCORRECT_DATA, "Bob should not have a receive chain key.");
		}
		std::cout << "Bob's initial send header key:\n";
		std::cout << bob_derived_keys.send_header_key.value() << std::endl;
		if (bob_derived_keys.receive_header_key.has_value()) {
			throw Exception(status_type::INCORRECT_DATA, "Bob should not have a receive header key.");
		}
		std::cout << "Bob's initial next send header key:\n";
		std::cout << bob_derived_keys.next_send_header_key << std::endl;
		std::cout << "Bob's initial next receive header key:\n";
		std::cout << bob_derived_keys.next_receive_header_key << std::endl;

		//compare Alice's and Bob's initial root key
		if (alice_derived_keys.root_key != bob_derived_keys.root_key) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "Alice's and Bob's initial root keys don't match."};
		}
		std::cout << "Alice's and Bob's initial root keys match.\n";

		//compare Alice's and Bob's initial chain keys
		if (alice_derived_keys.send_chain_key != bob_derived_keys.receive_chain_key) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "Alice's and Bob's initial chain keys don't match."};
		}
		std::cout << "Alice's and Bob's initial chain keys match.\n";

		if (alice_derived_keys.receive_chain_key != bob_derived_keys.send_chain_key) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "Alice's and Bob's initial chain keys don't match."};
		}
		std::cout << "Alice's and Bob's initial chain keys match.\n";

		//compare Alice's and Bob's initial header keys 1/2
		if (alice_derived_keys.send_header_key != bob_derived_keys.receive_header_key) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "Alice's initial send and Bob's initial receive header keys don't match."};
		}
		std::cout << "Alice's initial send and Bob's initial receive header keys match.\n";

		//compare Alice's and Bob's initial header keys 2/2
		if (alice_derived_keys.receive_header_key != bob_derived_keys.send_header_key) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "Alice's initial receive and Bob's initial send header keys don't match."};
		}
		std::cout << "Alice's initial receive and Bob's initial send header keys match.\n";

		//compare Alice's and Bob's initial next header keys 1/2
		if (alice_derived_keys.next_send_header_key != bob_derived_keys.next_receive_header_key) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "Alice's initial next send and Bob's initial next receive header keys don't match."};
		}
		std::cout << "Alice's initial next send and Bob's initial next receive header keys match.\n";

		//compare Alice's and Bob's initial next header keys 2/2
		if (alice_derived_keys.next_receive_header_key != bob_derived_keys.next_send_header_key) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "Alice's initial next receive and Bob's initial next send header keys don't match."};
		}
		std::cout << "Alice's initial next receive and Bob's initial next send header keys match.\n";
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

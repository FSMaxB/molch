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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sodium.h>
#include <iostream>
#include <exception>

#include "../lib/prekey-store.hpp"
#include "../lib/constants.h"
#include "../lib/molch-exception.hpp"
#include "../lib/destroyers.hpp"
#include "utils.hpp"

using namespace Molch;

static void protobuf_export(
		PrekeyStore& store,
		std::vector<Buffer>& key_buffers,
		std::vector<Buffer>& deprecated_key_buffers) {
		ProtobufCPrekey** keypairs = nullptr;
		ProtobufCPrekey** deprecated_keypairs = nullptr;

	ProtobufPool pool;
	size_t keypairs_size;
	size_t deprecated_keypairs_size;
	store.exportProtobuf(
		pool,
		keypairs,
		keypairs_size,
		deprecated_keypairs,
		deprecated_keypairs_size);

	//export all the keypairs
	key_buffers = std::vector<Buffer>();
	key_buffers.reserve(keypairs_size);
	for (size_t i = 0; i < keypairs_size; i++) {
		size_t export_size = prekey__get_packed_size(keypairs[i]);
		key_buffers.emplace_back(export_size, 0);

		key_buffers[i].size = prekey__pack(keypairs[i], key_buffers[i].content);
	}

	//export all the deprecated keypairs
	deprecated_key_buffers = std::vector<Buffer>();
	deprecated_key_buffers.reserve(deprecated_keypairs_size);
	for (size_t i = 0; i < deprecated_keypairs_size; i++) {
		size_t export_size = prekey__get_packed_size(deprecated_keypairs[i]);
		deprecated_key_buffers.emplace_back(export_size, 0);

		deprecated_key_buffers[i].size = prekey__pack(deprecated_keypairs[i], deprecated_key_buffers[i].content);
	}
}

static void protobuf_import(
		ProtobufPool& pool,
		std::unique_ptr<PrekeyStore>& store,
		const std::vector<Buffer>& keypair_buffers,
		const std::vector<Buffer>& deprecated_keypair_buffers) {
	auto pool_protoc_allocator = pool.getProtobufCAllocator();
	//parse the normal prekey protobufs
	auto keypairs_array = std::unique_ptr<ProtobufCPrekey*[]>(new ProtobufCPrekey*[keypair_buffers.size()]);
	size_t index = 0;
	for (const auto& keypair_buffer : keypair_buffers) {
		keypairs_array[index] = prekey__unpack(
				&pool_protoc_allocator,
				keypair_buffer.size,
				keypair_buffer.content);
		if (keypairs_array[index] == nullptr) {
			throw Molch::Exception(PROTOBUF_UNPACK_ERROR, "Failed to unpack prekey from protobuf.");
		}

		index++;
	}

	//parse the deprecated prekey protobufs
	auto deprecated_keypairs_array = std::unique_ptr<ProtobufCPrekey*[]>(new ProtobufCPrekey*[deprecated_keypair_buffers.size()]);
	index = 0;
	for (const auto& keypair_buffer : deprecated_keypair_buffers) {
		deprecated_keypairs_array[index] = prekey__unpack(
				&pool_protoc_allocator,
				keypair_buffer.size,
				keypair_buffer.content);
		if (deprecated_keypairs_array[index] == nullptr) {
			throw Molch::Exception(PROTOBUF_UNPACK_ERROR, "Failed to unpack deprecated prekey from protobuf.");
		}

		index++;
	}

	//now do the import
	store.reset(new PrekeyStore(
		keypairs_array.get(),
		keypair_buffers.size(),
		deprecated_keypairs_array.get(),
		deprecated_keypair_buffers.size()));
}

void protobuf_no_deprecated_keys(void) {
	printf("Testing im-/export of prekey store without deprecated keys.\n");
	PrekeyStore store;

	//export it
	ProtobufPool pool;
	ProtobufCPrekey **exported = nullptr;
	size_t exported_length = 0;
	ProtobufCPrekey **deprecated = nullptr;
	size_t deprecated_length = 0;
	store.exportProtobuf(pool, exported, exported_length, deprecated, deprecated_length);

	if ((deprecated != nullptr) || (deprecated_length != 0)) {
		throw Molch::Exception(INCORRECT_DATA, "Exported deprecated prekeys are not empty.");
	}

	//import it
	store = PrekeyStore(
		exported,
		exported_length,
		deprecated,
		deprecated_length);

	printf("Successful.\n");
}

int main(void) {
	try {
		if (sodium_init() == -1) {
			throw Molch::Exception(INIT_ERROR, "Failed to initialize libsodium.");
		}

		auto store = std::make_unique<PrekeyStore>();
		Buffer prekey_list(PREKEY_AMOUNT * PUBLIC_KEY_SIZE, PREKEY_AMOUNT * PUBLIC_KEY_SIZE);
		store->list(prekey_list);
		printf("Prekey list:\n");
		prekey_list.printHex(std::cout) << std::endl;

		//compare the public keys with the ones in the prekey store
		for (size_t i = 0; i < PREKEY_AMOUNT; i++) {
			if (prekey_list.compareToRawPartial(PUBLIC_KEY_SIZE * i, (*store->prekeys)[i].public_key.data(), (*store->prekeys)[i].public_key.size(), 0, PUBLIC_KEY_SIZE) != 0) {
				throw Molch::Exception(INCORRECT_DATA, "Key list doesn't match the prekey store.");
			}
		}
		printf("Prekey list matches the prekey store!\n");

		//get a private key
		const size_t prekey_index = 10;
		PublicKey public_prekey{(*store->prekeys)[prekey_index].public_key};

		PrivateKey private_prekey1;
		store->getPrekey(public_prekey, private_prekey1);
		printf("Get a Prekey:\n");
		printf("Public key:\n");
		public_prekey.printHex(std::cout);
		printf("Private key:\n");
		private_prekey1.printHex(std::cout) << std::endl;

		if (store->deprecated_prekeys.empty()) {
			throw Molch::Exception(GENERIC_ERROR, "Failed to deprecate requested key.");
		}

		if ((public_prekey != store->deprecated_prekeys[0].public_key)
				|| (private_prekey1 != store->deprecated_prekeys[0].private_key)) {
			throw Molch::Exception(INCORRECT_DATA, "Deprecated key is incorrect.");
		}

		if ((*store->prekeys)[prekey_index].public_key == public_prekey) {
			throw Molch::Exception(KEYGENERATION_FAILED, "Failed to generate new key for deprecated one.");
		}
		printf("Successfully deprecated requested key!\n");

		//check if the prekey can be obtained from the deprecated keys
		PrivateKey private_prekey2;
		store->getPrekey(public_prekey, private_prekey2);

		if (private_prekey1 != private_prekey2) {
			throw Molch::Exception(INCORRECT_DATA, "Prekey from the deprecated area didn't match.");
		}
		printf("Successfully got prekey from the deprecated area!\n");

		//try to get a nonexistent key
		public_prekey.fillRandom();
		bool found = true;
		try {
			store->getPrekey(public_prekey, private_prekey1);
		} catch (const Molch::Exception& exception) {
			found = false;
		}
		if (found) {
			throw Molch::Exception(GENERIC_ERROR, "Didn't complain about invalid public key.");
		}
		printf("Detected invalid public prekey!\n");

		//Protobuf-C export
		printf("Protobuf-C export\n");
		std::vector<Buffer> protobuf_export_prekeys_buffers;
		std::vector<Buffer> protobuf_export_deprecated_prekeys_buffers;
		protobuf_export(
			*store,
			protobuf_export_prekeys_buffers,
			protobuf_export_deprecated_prekeys_buffers);

		printf("Prekeys:\n");
		puts("[\n");
		for (size_t i = 0; i < protobuf_export_prekeys_buffers.size(); i++) {
			protobuf_export_prekeys_buffers[i].printHex(std::cout) << ",\n";
		}
		puts("]\n\n");

		printf("Deprecated Prekeys:\n");
		puts("[\n");
		for (size_t i = 0; i < protobuf_export_deprecated_prekeys_buffers.size(); i++) {
			protobuf_export_deprecated_prekeys_buffers[i].printHex(std::cout) << ",\n";
		}
		puts("]\n\n");

		store.reset();

		printf("Import from Protobuf-C\n");
		ProtobufPool pool;
		protobuf_import(
			pool,
			store,
			protobuf_export_prekeys_buffers,
			protobuf_export_deprecated_prekeys_buffers);

		printf("Protobuf-C export again\n");
		std::vector<Buffer> protobuf_second_export_prekeys_buffers;
		std::vector<Buffer> protobuf_second_export_deprecated_prekeys_buffers;
		protobuf_export(
			*store,
			protobuf_second_export_prekeys_buffers,
			protobuf_second_export_deprecated_prekeys_buffers);

		//compare both prekey lists
		printf("Compare normal prekeys\n");
		if (protobuf_export_prekeys_buffers.size() != protobuf_second_export_prekeys_buffers.size()) {
			throw Molch::Exception(INCORRECT_DATA, "Both prekey exports contain different amounts of keys.");
		}
		for (size_t i = 0; i < protobuf_export_prekeys_buffers.size(); i++) {
			if (protobuf_export_prekeys_buffers[i] != protobuf_second_export_prekeys_buffers[i]) {
				throw Molch::Exception(INCORRECT_DATA, "First and second prekey export are not identical.");
			}
		}

		//compare both deprecated prekey lists
		printf("Compare deprecated prekeys\n");
		if (protobuf_export_deprecated_prekeys_buffers.size() != protobuf_second_export_deprecated_prekeys_buffers.size()) {
			throw Molch::Exception(INCORRECT_DATA, "Both depcated prekey exports contain different amounts of keys.");
		}
		for (size_t i = 0; i < protobuf_export_deprecated_prekeys_buffers.size(); i++) {
			if (protobuf_export_deprecated_prekeys_buffers[i] != protobuf_second_export_deprecated_prekeys_buffers[i]) {
				throw Molch::Exception(INCORRECT_DATA, "First and second deprecated prekey export are not identical.");
			}
		}

		//test the automatic deprecation of old keys
		public_prekey = (*store->prekeys)[PREKEY_AMOUNT-1].public_key;

		(*store->prekeys)[PREKEY_AMOUNT-1].expiration_date -= 365 * 24 * 3600; //one year
		store->oldest_expiration_date = (*store->prekeys)[PREKEY_AMOUNT - 1].expiration_date;

		store->rotate();

		if (store->deprecated_prekeys.back().public_key != public_prekey) {
			throw Molch::Exception(GENERIC_ERROR, "Failed to deprecate outdated key.");
		}
		printf("Successfully deprecated outdated key!\n");

		//test the automatic removal of old deprecated keys!
		public_prekey = store->deprecated_prekeys[1].public_key;

		store->deprecated_prekeys[1].expiration_date -= 24 * 3600;
		store->oldest_deprecated_expiration_date = store->deprecated_prekeys[1].expiration_date;

		store->rotate();

		if (store->deprecated_prekeys.size() != 1) {
			throw Molch::Exception(GENERIC_ERROR, "Failed to remove outdated key.");
		}
		printf("Successfully removed outdated deprecated key!\n");

		protobuf_no_deprecated_keys();
	} catch (const Molch::Exception& exception) {
		exception.print(std::cerr) << std::endl;
		return EXIT_FAILURE;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

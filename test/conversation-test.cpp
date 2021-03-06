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
#include <memory>

#include "common.hpp"
#include "utils.hpp"
#include "exception.hpp"
#include "../lib/conversation.hpp"
#include "../lib/destroyers.hpp"
#include "../lib/key.hpp"

using namespace Molch;

static Buffer protobuf_export(const Molch::Conversation& conversation) {
	//export the conversation
	Arena pool;
	TRY_WITH_RESULT(exported_conversation_result, conversation.exportProtobuf(pool));
	const auto& exported_conversation{exported_conversation_result.value()};

	auto export_size{molch__protobuf__conversation__get_packed_size(exported_conversation)};
	Buffer export_buffer{export_size, 0};
	TRY_VOID(export_buffer.setSize(molch__protobuf__conversation__pack(exported_conversation, byte_to_uchar(export_buffer.data()))));
	if (export_size != export_buffer.size()) {
		throw Molch::Exception{status_type::PROTOBUF_PACK_ERROR, "Failed to pack protobuf-c struct into buffer."};
	}

	return export_buffer;
}

static Molch::Conversation protobuf_import(Arena& pool, const Buffer& import_buffer) {
	auto pool_protoc_allocator{pool.getProtobufCAllocator()};
	auto conversation_protobuf{molch__protobuf__conversation__unpack(
		&pool_protoc_allocator,
		import_buffer.size(),
		byte_to_uchar(import_buffer.data()))};
	if (!conversation_protobuf) {
		throw Molch::Exception{status_type::PROTOBUF_UNPACK_ERROR, "Failed to unpack conversation from protobuf."};
	}

	TRY_WITH_RESULT(exported_conversation_result, Molch::Conversation::import(*conversation_protobuf));
	return std::move(exported_conversation_result.value());
}

int main() noexcept {
	try {
		TRY_VOID(Molch::sodium_init());

		//creating charlie's identity keypair
		PrivateKey charlie_private_identity;
		PublicKey charlie_public_identity;
		generate_and_print_keypair(
			charlie_public_identity,
			charlie_private_identity,
			"Charlie",
			"identity");

		//creating charlie's ephemeral keypair
		PrivateKey charlie_private_ephemeral;
		PublicKey charlie_public_ephemeral;
		generate_and_print_keypair(
			charlie_public_ephemeral,
			charlie_private_ephemeral,
			"Charlie",
			"ephemeral");

		//creating dora's identity keypair
		PrivateKey dora_private_identity;
		PublicKey dora_public_identity;
		generate_and_print_keypair(
			dora_public_identity,
			dora_private_identity,
			"Dora",
			"identity");

		//creating dora's ephemeral keypair
		PrivateKey dora_private_ephemeral;
		PublicKey dora_public_ephemeral;
		generate_and_print_keypair(
			dora_public_ephemeral,
			dora_private_ephemeral,
			"Dora",
			"ephemeral");

		//create charlie's conversation
		TRY_WITH_RESULT(charlie_conversation_result, Molch::Conversation::create(
				charlie_private_identity,
				charlie_public_identity,
				dora_public_identity,
				charlie_private_ephemeral,
				charlie_public_ephemeral,
				dora_public_ephemeral));
		auto& charlie_conversation{charlie_conversation_result.value()};

		//create Dora's conversation
		TRY_WITH_RESULT(dora_conversation_result, Molch::Conversation::create(
				dora_private_identity,
				dora_public_identity,
				charlie_public_identity,
				dora_private_ephemeral,
				dora_public_ephemeral,
				charlie_public_ephemeral));

		//test protobuf-c export
		std::cout << "Export to Protobuf-C\n";
		auto protobuf_export_buffer{protobuf_export(charlie_conversation)};

		std::cout << protobuf_export_buffer;
		puts("\n");

		//import
		std::cout << "Import from Protobuf-C\n";
		Arena pool;
		charlie_conversation = protobuf_import(pool, protobuf_export_buffer);

		//export again
		std::cout << "Export again\n";
		auto protobuf_second_export_buffer{protobuf_export(charlie_conversation)};

		//compare
		if (protobuf_export_buffer != protobuf_second_export_buffer) {
			throw Molch::Exception{status_type::EXPORT_ERROR, "Both exported buffers are not the same."};
		}
		std::cout << "Both exported buffers are identitcal.\n\n";
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

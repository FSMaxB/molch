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
#include <memory>
#include <iostream>
#include <string_view>

#include "../lib/master-keys.hpp"
#include "molch/constants.h"
#include "utils.hpp"
#include "inline-utils.hpp"
#include "exception.hpp"

using namespace Molch;

static void protobuf_export(
		MasterKeys& keys,
		Buffer& public_signing_key_buffer,
		Buffer& private_signing_key_buffer,
		Buffer& public_identity_key_buffer,
		Buffer& private_identity_key_buffer) {
	Arena arena;
	TRY_WITH_RESULT(exported_result, keys.exportProtobuf(arena));
	const auto& exported{exported_result.value()};

	//copy keys to buffer
	//public signing key
	auto public_signing_key_proto_size{molch__protobuf__key__get_packed_size(exported.public_signing_key)};
	public_signing_key_buffer = Buffer{public_signing_key_proto_size, 0};
	TRY_VOID(public_signing_key_buffer.setSize(molch__protobuf__key__pack(exported.public_signing_key, byte_to_uchar(public_signing_key_buffer.data()))));
	if (public_signing_key_buffer.size() != public_signing_key_proto_size) {
		throw Molch::Exception{status_type::EXPORT_ERROR, "Failed to export public signing key."};
	}

	//private signing key
	auto private_signing_key_proto_size{molch__protobuf__key__get_packed_size(exported.private_signing_key)};
	private_signing_key_buffer = Buffer{private_signing_key_proto_size, 0};
	TRY_VOID(private_signing_key_buffer.setSize(molch__protobuf__key__pack(exported.private_signing_key, byte_to_uchar(private_signing_key_buffer.data()))));
	if (private_signing_key_buffer.size() != private_signing_key_proto_size) {
		throw Molch::Exception{status_type::EXPORT_ERROR, "Failed to export private signing key."};
	}

	//public identity key
	auto public_identity_key_proto_size{molch__protobuf__key__get_packed_size(exported.public_identity_key)};
	public_identity_key_buffer = Buffer{public_identity_key_proto_size, 0};
	TRY_VOID(public_identity_key_buffer.setSize(molch__protobuf__key__pack(exported.public_identity_key, byte_to_uchar(public_identity_key_buffer.data()))));
	if (public_identity_key_buffer.size() != public_identity_key_proto_size) {
		throw Molch::Exception{status_type::EXPORT_ERROR, "Failed to export public identity key."};
	}

	//private identity key
	auto private_identity_key_proto_size{molch__protobuf__key__get_packed_size(exported.private_identity_key)};
	private_identity_key_buffer = Buffer{private_identity_key_proto_size, 0};
	TRY_VOID(private_identity_key_buffer.setSize(molch__protobuf__key__pack(exported.private_identity_key, byte_to_uchar(private_identity_key_buffer.data()))));
	if (private_identity_key_buffer.size() != private_identity_key_proto_size) {
		throw Molch::Exception{status_type::EXPORT_ERROR, "Failed to export private identity key."};
	}
}


static MasterKeys protobuf_import(
		Arena& pool,
		const Buffer& public_signing_key_buffer,
		const Buffer& private_signing_key_buffer,
		const Buffer& public_identity_key_buffer,
		const Buffer& private_identity_key_buffer) {
	auto pool_protoc_allocator{pool.getProtobufCAllocator()};

	//unpack the protobuf-c buffers
	auto public_signing_key{molch__protobuf__key__unpack(
			&pool_protoc_allocator,
			public_signing_key_buffer.size(),
			byte_to_uchar(public_signing_key_buffer.data()))};
	if (public_signing_key == nullptr) {
		throw Molch::Exception{status_type::PROTOBUF_UNPACK_ERROR, "Failed to unpack public signing key from protobuf."};
	}
	auto private_signing_key{molch__protobuf__key__unpack(
			&pool_protoc_allocator,
			private_signing_key_buffer.size(),
			byte_to_uchar(private_signing_key_buffer.data()))};
	if (private_signing_key == nullptr) {
		throw Molch::Exception{status_type::PROTOBUF_UNPACK_ERROR, "Failed to unpack private signing key from protobuf."};
	}
	auto public_identity_key{molch__protobuf__key__unpack(
			&pool_protoc_allocator,
			public_identity_key_buffer.size(),
			byte_to_uchar(public_identity_key_buffer.data()))};
	if (public_identity_key == nullptr) {
		throw Molch::Exception{status_type::PROTOBUF_UNPACK_ERROR, "Failed to unpack public identity key from protobuf."};
	}
	auto private_identity_key{molch__protobuf__key__unpack(
			&pool_protoc_allocator,
			private_identity_key_buffer.size(),
			byte_to_uchar(private_identity_key_buffer.data()))};
	if (private_identity_key == nullptr) {
		throw Molch::Exception{status_type::PROTOBUF_UNPACK_ERROR, "Failed to unpack private identity key from protobuf."};
	}

	TRY_WITH_RESULT(keys_result, MasterKeys::import(
			*public_signing_key,
			*private_signing_key,
			*public_identity_key,
			*private_identity_key));
	return std::move(keys_result.value());
}


int main() {
	try {
		TRY_VOID(Molch::sodium_init());
		//create the unspiced master keys
		TRY_WITH_RESULT(unspiced_master_keys_result, MasterKeys::create());
		auto& unspiced_master_keys{unspiced_master_keys_result.value()};

		//get the public keys
		PublicSigningKey public_signing_key{unspiced_master_keys.getSigningKey()};
		PublicKey public_identity_key{unspiced_master_keys.getIdentityKey()};

		//print the keys
		std::cout << "Signing keypair:\n";
		std::cout << "Public:\n";
		std::cout << unspiced_master_keys.getSigningKey();

		std::cout << "\nPrivate:\n";
		{
			MasterKeys::Unlocker unlocker{unspiced_master_keys};
			TRY_WITH_RESULT(private_signing_key, unspiced_master_keys.getPrivateSigningKey());
			std::cout << private_signing_key.value();
		}

		std::cout << "\n\nIdentity keys:\n";
		std::cout << "Public:\n";
		std::cout << unspiced_master_keys.getIdentityKey();

		std::cout << "\nPrivate:\n";
		{
			MasterKeys::Unlocker unlocker{unspiced_master_keys};
			TRY_WITH_RESULT(private_identity_key, unspiced_master_keys.getPrivateIdentityKey());
			std::cout << *private_identity_key.value();
		}

		//check the exported public keys
		if (public_signing_key != unspiced_master_keys.getSigningKey()) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "Exported public signing key doesn't match."};
		}
		if (public_identity_key != unspiced_master_keys.getIdentityKey()) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "Exported public identity key doesn't match."};
		}


		//create the spiced master keys
		Buffer seed{";a;awoeih]]pquw4t[spdif\\aslkjdf;'ihdg#)%!@))%)#)(*)@)#)h;kuhe[orih;o's':ke';sa'd;kfa';;.calijv;a/orq930u[sd9f0u;09[02;oasijd;adk"};
		TRY_WITH_RESULT(spiced_master_keys_result, MasterKeys::create(seed));
		auto& spiced_master_keys{spiced_master_keys_result.value()};
		public_signing_key = spiced_master_keys.getSigningKey();
		public_identity_key = spiced_master_keys.getIdentityKey();

		//print the keys
		std::cout << "Signing keypair:\n";
		std::cout << "Public:\n";
		std::cout << spiced_master_keys.getSigningKey() << std::endl;

		std::cout << "Private:\n";
		{
			MasterKeys::Unlocker unlocker{spiced_master_keys};
			TRY_WITH_RESULT(private_signing_key, spiced_master_keys.getPrivateSigningKey());
			std::cout << private_signing_key.value() << '\n';
		}

		std::cout << "\nIdentity keys:\n";
		std::cout << "Public:\n";
		std::cout << spiced_master_keys.getIdentityKey() << std::endl;

		std::cout << "Private:\n";
		{
			MasterKeys::Unlocker unlocker{spiced_master_keys};
			TRY_WITH_RESULT(private_identity_key, spiced_master_keys.getPrivateIdentityKey());
			std::cout << *private_identity_key.value();
		}

		//check the exported public keys
		if (public_signing_key != spiced_master_keys.getSigningKey()) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "Exported public signing key doesn't match."};
		}
		if (public_identity_key != spiced_master_keys.getIdentityKey()) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "Exported public identity key doesn't match."};
		}

		//sign some data
		Buffer data{"This is some data to be signed."};
		std::cout << "Data to be signed.\n";
		std::cout << std::string_view(byte_to_char(data.data()), data.size()) << '\n';
		TRY_WITH_RESULT(signed_data_result, spiced_master_keys.sign(data));
		const auto& signed_data{signed_data_result.value()};
		std::cout << "Signed data:\n";
		std::cout << signed_data;

		//now check the signature
		Buffer unwrapped_data{100, 0};
		unsigned long long unwrapped_data_length;
		auto status{crypto_sign_open(
				byte_to_uchar(unwrapped_data.data()),
				&unwrapped_data_length,
				byte_to_uchar(signed_data.data()),
				signed_data.size(),
				byte_to_uchar(public_signing_key.data()))};
		if (status != 0) {
			throw Molch::Exception{status_type::VERIFY_ERROR, "Failed to verify signature."};
		}
		TRY_VOID(unwrapped_data.setSize(static_cast<size_t>(unwrapped_data_length)));

		std::cout << "\nSignature was successfully verified!\n";

		//Test Export to Protobuf-C
		std::cout << "Export to Protobuf-C:\n";

		//export buffers
		Buffer protobuf_export_public_signing_key;
		Buffer protobuf_export_private_signing_key;
		Buffer protobuf_export_public_identity_key;
		Buffer protobuf_export_private_identity_key;
		protobuf_export(
			spiced_master_keys,
			protobuf_export_public_signing_key,
			protobuf_export_private_signing_key,
			protobuf_export_public_identity_key,
			protobuf_export_private_identity_key);

		std::cout << "Public signing key:\n";
		std::cout << protobuf_export_public_signing_key << "\n\n";

		std::cout << "Private signing key:\n";
		std::cout << protobuf_export_private_signing_key << "\n\n";

		std::cout << "Public identity key:\n";
		std::cout << protobuf_export_public_identity_key << "\n\n";

		std::cout << "Private identity key:\n";
		std::cout << protobuf_export_private_identity_key << "\n\n";

		//import again
		std::cout << "Import from Protobuf-C:\n";
		Arena pool;
		auto imported_master_keys{protobuf_import(
			pool,
			protobuf_export_public_signing_key,
			protobuf_export_private_signing_key,
			protobuf_export_public_identity_key,
			protobuf_export_private_identity_key)};

		//export again
		Buffer protobuf_second_export_public_signing_key;
		Buffer protobuf_second_export_private_signing_key;
		Buffer protobuf_second_export_public_identity_key;
		Buffer protobuf_second_export_private_identity_key;
		protobuf_export(
			imported_master_keys,
			protobuf_second_export_public_signing_key,
			protobuf_second_export_private_signing_key,
			protobuf_second_export_public_identity_key,
			protobuf_second_export_private_identity_key);

		//now compare
		if (protobuf_export_public_signing_key != protobuf_second_export_public_signing_key) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "The public signing keys do not match."};
		}
		if (protobuf_export_private_signing_key != protobuf_second_export_private_signing_key) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "The private signing keys do not match."};
		}
		if (protobuf_export_public_identity_key != protobuf_second_export_public_identity_key) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "The public identity keys do not match."};
		}
		if (protobuf_export_private_identity_key != protobuf_second_export_private_identity_key) {
			throw Molch::Exception{status_type::INCORRECT_DATA, "The private identity keys do not match."};
		}

		std::cout << "Successfully exported to Protobuf-C and imported again.";
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

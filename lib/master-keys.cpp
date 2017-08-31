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

#include <sodium.h>
#include <exception>
#include <memory>
#include <sstream>

#include "molch-exception.hpp"
#include "master-keys.hpp"
#include "spiced-random.hpp"
#include "sodium-wrappers.hpp"
#include "autozero.hpp"
#include "gsl.hpp"

namespace Molch {
	MasterKeys& MasterKeys::move(MasterKeys&& master_keys) {
		//move the private keys
		this->private_keys = std::move(master_keys.private_keys);
		this->private_identity_key = master_keys.private_identity_key;
		this->private_signing_key = master_keys.private_signing_key;

		this->public_identity_key = master_keys.public_identity_key;
		this->public_signing_key = master_keys.public_signing_key;

		return *this;
	}

	MasterKeys::MasterKeys(MasterKeys&& master_keys) {
		this->move(std::move(master_keys));
	}

	MasterKeys& MasterKeys::operator=(MasterKeys&& master_keys) {
		return this->move(std::move(master_keys));
	}

	MasterKeys::MasterKeys() {
		this->init();
		this->generate();
	}

	MasterKeys::MasterKeys(const gsl::span<const gsl::byte> low_entropy_seed) {
		this->init();
		this->generate(low_entropy_seed);
	}

	MasterKeys::MasterKeys(
			const ProtobufCKey& public_signing_key,
			const ProtobufCKey& private_signing_key,
			const ProtobufCKey& public_identity_key,
			const ProtobufCKey& private_identity_key) {
		this->init();

		if ((this->private_signing_key == nullptr) || (this->private_identity_key == nullptr)) {
			throw Exception{status_type::INCORRECT_DATA, "One of the private key pointers is nullptr."};
		}

		ReadWriteUnlocker unlocker{*this};

		//copy the keys
		this->public_signing_key.set({
				uchar_to_byte(public_signing_key.key.data),
				narrow(public_signing_key.key.len)});
		this->public_identity_key.set({
				uchar_to_byte(public_identity_key.key.data),
				narrow(public_identity_key.key.len)});
		this->private_signing_key->set({
				uchar_to_byte(private_signing_key.key.data),
				narrow(private_signing_key.key.len)});
		this->private_identity_key->set({
				uchar_to_byte(private_identity_key.key.data),
				narrow(private_identity_key.key.len)});
	}


	void MasterKeys::init() {
		//allocate the private key storage
		this->private_keys = std::unique_ptr<PrivateMasterKeyStorage,SodiumDeleter<PrivateMasterKeyStorage>>(throwing_sodium_malloc<PrivateMasterKeyStorage>(1));

		//initialize the Buffers
		//private, initialize with pointers to private key storage
		this->private_identity_key = &this->private_keys->identity_key;
		this->private_signing_key = &this->private_keys->signing_key;

		//lock the private key storage
		this->lock();
	}

	void MasterKeys::generate() {
		ReadWriteUnlocker unlocker{*this};

		//generate the signing keypair
		auto status{crypto_sign_keypair(
				byte_to_uchar(this->public_signing_key.data()),
				byte_to_uchar(this->private_signing_key->data()))};
		if (status != 0) {
			throw Exception{status_type::KEYGENERATION_FAILED, "Failed to generate signing keypair."};
		}
		this->public_signing_key.empty = false;
		this->private_signing_key->empty = false;

		//generate the identity keypair
		status = crypto_box_keypair(
				byte_to_uchar(this->public_identity_key.data()),
				byte_to_uchar(this->private_identity_key->data()));
		if (status != 0) {
			throw Exception{status_type::KEYGENERATION_FAILED, "Failed to generate identity keypair."};
		}
		this->public_identity_key.empty = false;
		this->private_identity_key->empty = false;
	}

	void MasterKeys::generate(const gsl::span<const gsl::byte> low_entropy_seed) {
		Expects(!low_entropy_seed.empty());

		ReadWriteUnlocker unlocker{*this};

		SodiumBuffer high_entropy_seed{
				crypto_sign_SEEDBYTES + crypto_box_SEEDBYTES,
				crypto_sign_SEEDBYTES + crypto_box_SEEDBYTES};
		spiced_random(high_entropy_seed, low_entropy_seed);

		//generate the signing keypair
		auto status{crypto_sign_seed_keypair(
				byte_to_uchar(this->public_signing_key.data()),
				byte_to_uchar(this->private_signing_key->data()),
				byte_to_uchar(high_entropy_seed.data()))};
		if (status != 0) {
			throw Exception{status_type::KEYGENERATION_FAILED, "Failed to generate signing keypair with seed."};
		}
		this->public_signing_key.empty = false;
		this->private_signing_key->empty = false;

		//generate the identity keypair
		status = crypto_box_seed_keypair(
				byte_to_uchar(this->public_identity_key.data()),
				byte_to_uchar(this->private_identity_key->data()),
				byte_to_uchar(&high_entropy_seed[crypto_sign_SEEDBYTES]));
		if (status != 0) {
			throw Exception{status_type::KEYGENERATION_FAILED, "Failed to generate identity keypair with seed."};
		}
		this->public_identity_key.empty = false;
		this->private_identity_key->empty = false;
	}

	/*
	 * Get the public signing key.
	 */
	void MasterKeys::getSigningKey(PublicSigningKey& public_signing_key) const {
		public_signing_key = this->public_signing_key;
	}

	/*
	 * Get the public identity key.
	 */
	void MasterKeys::getIdentityKey(PublicKey& public_identity_key) const {
		public_identity_key = this->public_identity_key;
	}

	/*
	 * Sign a piece of data. Returns the data and signature in one output buffer.
	 */
	void MasterKeys::sign(
			const gsl::span<const gsl::byte> data,
			gsl::span<gsl::byte> signed_data) const { //output, length of data + SIGNATURE_SIZE
		Expects(signed_data.size() == (data.size() + SIGNATURE_SIZE));

		Unlocker unlocker{*this};
		unsigned long long signed_message_length;
		auto status{crypto_sign(
			byte_to_uchar(signed_data.data()),
			&signed_message_length,
			byte_to_uchar(data.data()),
			narrow(data.size()),
			byte_to_uchar(this->private_signing_key->data()))};
		if (status != 0) {
			throw Exception{status_type::SIGN_ERROR, "Failed to sign message."};
		}
	}

	void MasterKeys::exportProtobuf(
			ProtobufPool& pool,
			ProtobufCKey*& public_signing_key,
			ProtobufCKey*& private_signing_key,
			ProtobufCKey*& public_identity_key,
			ProtobufCKey*& private_identity_key) const {
		//create and initialize the structs
		public_signing_key = pool.allocate<ProtobufCKey>(1);
		key__init(public_signing_key);
		private_signing_key = pool.allocate<ProtobufCKey>(1);
		key__init(private_signing_key);
		public_identity_key = pool.allocate<ProtobufCKey>(1);
		key__init(public_identity_key);
		private_identity_key = pool.allocate<ProtobufCKey>(1);
		key__init(private_identity_key);

		//allocate the key buffers
		public_signing_key->key.data = pool.allocate<uint8_t>(PUBLIC_MASTER_KEY_SIZE);
		public_signing_key->key.len = PUBLIC_MASTER_KEY_SIZE;
		private_signing_key->key.data = pool.allocate<uint8_t>(PRIVATE_MASTER_KEY_SIZE);
		private_signing_key->key.len = PRIVATE_MASTER_KEY_SIZE;
		public_identity_key->key.data = pool.allocate<uint8_t>(PUBLIC_KEY_SIZE);
		public_identity_key->key.len = PUBLIC_KEY_SIZE;
		private_identity_key->key.data = pool.allocate<uint8_t>(PRIVATE_KEY_SIZE);
		private_identity_key->key.len = PRIVATE_KEY_SIZE;

		//copy the keys
		this->public_signing_key.copyTo({uchar_to_byte(public_signing_key->key.data), PUBLIC_MASTER_KEY_SIZE});
		this->public_identity_key.copyTo({uchar_to_byte(public_identity_key->key.data), PUBLIC_KEY_SIZE});
		Unlocker unlocker{*this};
		this->private_signing_key->copyTo({uchar_to_byte(private_signing_key->key.data), PRIVATE_MASTER_KEY_SIZE});
		this->private_identity_key->copyTo({uchar_to_byte(private_identity_key->key.data), PRIVATE_KEY_SIZE});
	}

	void MasterKeys::lock() const {
		auto status{sodium_mprotect_noaccess(this->private_keys.get())};
		if (status != 0) {
			throw Exception{status_type::GENERIC_ERROR, "Failed to lock memory."};
		}
	}

	void MasterKeys::unlock() const {
		auto status{sodium_mprotect_readonly(this->private_keys.get())};
		if (status != 0) {
			throw Exception{status_type::GENERIC_ERROR, "Failed to make memory readonly."};
		}
	}

	void MasterKeys::unlock_readwrite() const {
		auto status{sodium_mprotect_readwrite(this->private_keys.get())};
		if (status != 0) {
			throw Exception{status_type::GENERIC_ERROR, "Failed to make memory readwrite."};
		}
	}

	std::ostream& MasterKeys::print(std::ostream& stream) const {
		Unlocker unlocker{*this};

		stream << "Public Signing Key:\n";
		this->public_signing_key.printHex(stream) << '\n';
		stream << "Private Signing Key:\n";
		this->private_signing_key->printHex(stream) << '\n';
		stream << "Public Identity Key:\n";
		this->public_identity_key.printHex(stream) << '\n';
		stream << "Private Identity Key:\n";
		this->private_identity_key->printHex(stream) << '\n';

		return stream;
	}

	MasterKeys::Unlocker::Unlocker(const MasterKeys& keys) : keys{keys} {
		this->keys.unlock();
	}

	MasterKeys::Unlocker::~Unlocker() {
		this->keys.lock();
	}

	MasterKeys::ReadWriteUnlocker::ReadWriteUnlocker(const MasterKeys& keys) : keys{keys} {
		this->keys.unlock_readwrite();
	}

	MasterKeys::ReadWriteUnlocker::~ReadWriteUnlocker() {
		this->keys.lock();
	}
}

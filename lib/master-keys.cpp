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

#include "master-keys.hpp"
#include "spiced-random.hpp"
#include "sodium-wrappers.hpp"
#include "gsl.hpp"

namespace Molch {
	MasterKeys& MasterKeys::move(MasterKeys&& master_keys) noexcept {
		//move the private keys
		this->private_keys = std::move(master_keys.private_keys);
		this->private_identity_key = master_keys.private_identity_key;
		this->private_signing_key = master_keys.private_signing_key;

		this->public_identity_key = master_keys.public_identity_key;
		this->public_signing_key = master_keys.public_signing_key;

		return *this;
	}

	MasterKeys::MasterKeys(MasterKeys&& master_keys) noexcept {
		this->move(std::move(master_keys));
	}

	MasterKeys& MasterKeys::operator=(MasterKeys&& master_keys) noexcept {
		this->move(std::move(master_keys));
		return *this;
	}

	MasterKeys::MasterKeys([[maybe_unused]] uninitialized_t uninitialized) noexcept {}

	result<MasterKeys> MasterKeys::create(const std::optional<span<const std::byte>> low_entropy_seed) {
		MasterKeys keys(uninitialized);
		keys.init();
		if (low_entropy_seed.has_value()) {
			OUTCOME_TRY(keys.generate(low_entropy_seed.value()));
		} else {
			OUTCOME_TRY(keys.generate());
		}

		return keys;
	}

	result<MasterKeys> MasterKeys::import(
			const ProtobufCKey& public_signing_key,
			const ProtobufCKey& private_signing_key,
			const ProtobufCKey& public_identity_key,
			const ProtobufCKey& private_identity_key) {
		MasterKeys keys(uninitialized);
		keys.init();

		if ((keys.private_signing_key == nullptr) || (keys.private_identity_key == nullptr)) {
			return Error(status_type::INCORRECT_DATA, "One of the private key pointers is nullptr.");
		}

		{
			ReadWriteUnlocker unlocker(keys);

			//copy the keys
			OUTCOME_TRY(imported_public_signing_key, PublicSigningKey::import(public_signing_key));
			keys.public_signing_key = imported_public_signing_key;
			OUTCOME_TRY(imported_private_signing_key, PrivateSigningKey::import(private_signing_key));
			*keys.private_signing_key = imported_private_signing_key;
			OUTCOME_TRY(imported_public_identity_key, PublicKey::import(public_identity_key));
			keys.public_identity_key = imported_public_identity_key;
			OUTCOME_TRY(imported_private_identity_key, PrivateKey::import(private_identity_key));
			*keys.private_identity_key = imported_private_identity_key;
		}

		return keys;
	}


	void MasterKeys::init() {
		//allocate the private key storage
		this->private_keys = std::unique_ptr<PrivateMasterKeyStorage,SodiumDeleter<PrivateMasterKeyStorage>>(sodium_malloc<PrivateMasterKeyStorage>(1));

		//initialize the Buffers
		//private, initialize with pointers to private key storage
		this->private_identity_key = &this->private_keys->identity_key;
		this->private_signing_key = &this->private_keys->signing_key;

		//lock the private key storage
		this->lock();
	}

	result<void> MasterKeys::generate() noexcept {
		ReadWriteUnlocker unlocker{*this};

		//generate the signing keypair
		OUTCOME_TRY(crypto_sign_keypair(
				this->public_signing_key,
				*this->private_signing_key));

		//generate the identity keypair
		OUTCOME_TRY(crypto_box_keypair(this->public_identity_key, *this->private_identity_key));

		return outcome::success();
	}

	result<void> MasterKeys::generate(const span<const std::byte> low_entropy_seed) noexcept {
		FulfillOrFail(!low_entropy_seed.empty());

		ReadWriteUnlocker unlocker{*this};

		OUTCOME_TRY(high_entropy_seed, spiced_random(low_entropy_seed, crypto_sign_SEEDBYTES + crypto_box_SEEDBYTES));

		//generate the signing keypair
		OUTCOME_TRY(crypto_sign_seed_keypair(
				this->public_signing_key,
				*this->private_signing_key,
				span<std::byte>(high_entropy_seed).subspan(0, crypto_sign_SEEDBYTES)));

		//generate the identity keypair
		OUTCOME_TRY(crypto_box_seed_keypair(
				this->public_identity_key,
				*this->private_identity_key,
				span<const std::byte>{high_entropy_seed}.subspan(crypto_sign_SEEDBYTES)));

		return outcome::success();
	}

	const PublicSigningKey& MasterKeys::getSigningKey() const noexcept {
		return this->public_signing_key;
	}

	result<const PrivateSigningKey*> MasterKeys::getPrivateSigningKey() const noexcept {
		if (this->private_signing_key == nullptr) {
			return Error(status_type::INCORRECT_DATA, "The private signing key pointer doesn't point to anything.");
		}

		return this->private_signing_key;
	}

	const PublicKey& MasterKeys::getIdentityKey() const noexcept {
		return this->public_identity_key;
	}

	result<const PrivateKey*> MasterKeys::getPrivateIdentityKey() const noexcept {
		if (this->private_identity_key == nullptr) {
			return Error(status_type::INCORRECT_DATA, "The private identity key pointer doesn't point to anything.");
		}

		return this->private_identity_key;
	}

	/*
	 * Sign a piece of data. Returns the data and signature in one output buffer.
	 */
	result<Buffer> MasterKeys::sign(const span<const std::byte> data) const {
		Unlocker unlocker{*this};
		const auto buffer_size{data.size() + SIGNATURE_SIZE};
		Buffer signed_data(buffer_size, buffer_size);
		OUTCOME_TRY(crypto_sign(
				signed_data,
				data,
				*this->private_signing_key));

		return signed_data;
	}

	result<MasterKeys::ExportedMasterKeys> MasterKeys::exportProtobuf(Arena& arena) const {
		Unlocker unlocker{*this};

		ExportedMasterKeys exported;

		OUTCOME_TRY(public_signing_key, this->public_signing_key.exportProtobuf(arena));
		exported.public_signing_key = public_signing_key;
		OUTCOME_TRY(private_signing_key, this->private_signing_key->exportProtobuf(arena));
		exported.private_signing_key = private_signing_key;
		OUTCOME_TRY(public_identity_key, this->public_identity_key.exportProtobuf(arena));
		exported.public_identity_key = public_identity_key;
		OUTCOME_TRY(private_identity_key, this->private_identity_key->exportProtobuf(arena));
		exported.private_identity_key = private_identity_key;

		return exported;
	}

	void MasterKeys::lock() const noexcept {
		if (this->private_keys) {
			sodium_mprotect_noaccess(this->private_keys.get());
		}
	}

	void MasterKeys::unlock() const noexcept {
		sodium_mprotect_readonly(this->private_keys.get());
	}

	void MasterKeys::unlock_readwrite() const noexcept {
		sodium_mprotect_readwrite(this->private_keys.get());
	}

	std::ostream& MasterKeys::print(std::ostream& stream) const {
		Unlocker unlocker{*this};

		stream << "Public Signing Key:\n";
		stream << this->public_signing_key << '\n';
		stream << "Private Signing Key:\n";
		stream << *this->private_signing_key << '\n';
		stream << "Public Identity Key:\n";
		stream << this->public_identity_key << '\n';
		stream << "Private Identity Key:\n";
		stream << this->private_identity_key << '\n';

		return stream;
	}

	MasterKeys::Unlocker::Unlocker(const MasterKeys& keys) noexcept : keys{keys} {
		this->keys.unlock();
	}

	MasterKeys::Unlocker::~Unlocker() noexcept {
		this->keys.lock();
	}

	MasterKeys::ReadWriteUnlocker::ReadWriteUnlocker(const MasterKeys& keys) noexcept : keys{keys} {
		this->keys.unlock_readwrite();
	}

	MasterKeys::ReadWriteUnlocker::~ReadWriteUnlocker() noexcept {
		this->keys.lock();
	}
}

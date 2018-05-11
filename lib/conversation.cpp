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

#include <exception>
#include <iterator>

#include "constants.h"
#include "conversation.hpp"
#include "packet.hpp"
#include "header.hpp"
#include "molch-exception.hpp"
#include "destroyers.hpp"
#include "gsl.hpp"

namespace Molch {
	Conversation& Conversation::move(Conversation&& conversation) noexcept {
		this->id_storage = conversation.id_storage;
		this->ratchet_pointer = std::move(conversation.ratchet_pointer);

		return *this;
	}

	Conversation::Conversation(Conversation&& conversation) noexcept {
		this->move(std::move(conversation));
	}

	Conversation& Conversation::operator=(Conversation&& conversation) noexcept {
		this->move(std::move(conversation));
		return *this;
	}

	/*
	 * Create a new conversation.
	 */
	void Conversation::create(
			const PrivateKey& our_private_identity,
			const PublicKey& our_public_identity,
			const PublicKey& their_public_identity,
			const PrivateKey& our_private_ephemeral,
			const PublicKey& our_public_ephemeral,
			const PublicKey& their_public_ephemeral) {
		Expects(!our_private_identity.empty
				&& !our_public_identity.empty
				&& !their_public_identity.empty
				&& !our_public_ephemeral.empty
				&& !our_public_ephemeral.empty
				&& !their_public_ephemeral.empty);

		//create random id
		this->id_storage.fillRandom();

		this->ratchet_pointer = std::make_unique<Ratchet>(
				our_private_identity,
				our_public_identity,
				their_public_identity,
				our_private_ephemeral,
				our_public_ephemeral,
				their_public_ephemeral);
	}

	Conversation::Conversation(
			const PrivateKey& our_private_identity,
			const PublicKey& our_public_identity,
			const PublicKey& their_public_identity,
			const PrivateKey& our_private_ephemeral,
			const PublicKey& our_public_ephemeral,
			const PublicKey& their_public_ephemeral) {
		this->create(
			our_private_identity,
			our_public_identity,
			their_public_identity,
			our_private_ephemeral,
			our_public_ephemeral,
			their_public_ephemeral);
	}

	/*
	 * Start a new conversation where we are the sender.
	 *
	 * Don't forget to destroy the return status with return_status_destroy_errors()
	 * if an error has occurred.
	 */
	Conversation::Conversation(
			const span<const std::byte> message, //message we want to send to the receiver
			Buffer& packet, //output
			const PublicKey& sender_public_identity, //who is sending this message?
			const PrivateKey& sender_private_identity,
			const PublicKey& receiver_public_identity,
			const span<const std::byte> receiver_prekey_list) { //PREKEY_AMOUNT * PUBLIC_KEY_SIZE
		Expects(!receiver_public_identity.empty
				&& !sender_public_identity.empty
				&& !sender_private_identity.empty
				&& (receiver_prekey_list.size() == (PREKEY_AMOUNT * PUBLIC_KEY_SIZE)));

		//create an ephemeral keypair
		PublicKey sender_public_ephemeral;
		PrivateKey sender_private_ephemeral;
		crypto_box_keypair(sender_public_ephemeral, sender_private_ephemeral);
		sender_public_ephemeral.empty = false;
		sender_private_ephemeral.empty = false;

		//choose a prekey
		auto prekey_number{randombytes_uniform(PREKEY_AMOUNT)};
		PublicKey receiver_public_prekey;
		receiver_public_prekey.set({
				&receiver_prekey_list[gsl::narrow_cast<ptrdiff_t>(prekey_number * PUBLIC_KEY_SIZE)],
				PUBLIC_KEY_SIZE});

		//initialize the conversation
		this->create(
				sender_private_identity,
				sender_public_identity,
				receiver_public_identity,
				sender_private_ephemeral,
				sender_public_ephemeral,
				receiver_public_prekey);

		packet = this->send(
				message,
				&sender_public_identity,
				&sender_public_ephemeral,
				&receiver_public_prekey);
	}

	/*
	 * Start a new conversation where we are the receiver.
	 *
	 * Don't forget to destroy the return status with return_status_destroy_errors()
	 * if an error has occurred.
	 */
	Conversation::Conversation(
			const span<const std::byte> packet, //received packet
			Buffer& message, //output
			const PublicKey& receiver_public_identity,
			const PrivateKey& receiver_private_identity,
			PrekeyStore& receiver_prekeys) { //prekeys of the receiver
		Expects(!receiver_public_identity.empty
				&& !receiver_private_identity.empty);

		uint32_t receive_message_number{0};
		uint32_t previous_receive_message_number{0};

		//get the senders keys and our public prekey from the packet
		PublicKey sender_public_identity;
		PublicKey receiver_public_prekey;
		PublicKey sender_public_ephemeral;
		molch_message_type packet_type;
		uint32_t current_protocol_version;
		uint32_t highest_supported_protocol_version;
		packet_get_metadata_without_verification(
			current_protocol_version,
			highest_supported_protocol_version,
			packet_type,
			packet,
			&sender_public_identity,
			&sender_public_ephemeral,
			&receiver_public_prekey);

		if (packet_type != molch_message_type::PREKEY_MESSAGE) {
			throw Exception{status_type::INVALID_VALUE, "Packet is not a prekey message."};
		}

		//get the private prekey that corresponds to the public prekey used in the message
		PrivateKey receiver_private_prekey;
		receiver_prekeys.getPrekey(receiver_public_prekey, receiver_private_prekey);

		this->create(
				receiver_private_identity,
				receiver_public_identity,
				sender_public_identity,
				receiver_private_prekey,
				receiver_public_prekey,
				sender_public_ephemeral);

		message = this->receive(
				packet,
				receive_message_number,
				previous_receive_message_number);
	}

	/*
	 * Send a message using an existing conversation.
	 *
	 * Don't forget to destroy the return status with return_status_destroy_errors()
	 * if an error has occurred.
	 */
	Buffer Conversation::send(
			const span<const std::byte> message,
			const PublicKey * const public_identity_key, //can be nullptr, if not nullptr, this will be a prekey message
			const PublicKey * const public_ephemeral_key, //can be nullptr, if not nullptr, this will be a prekey message
			const PublicKey * const public_prekey) { //can be nullptr, if not nullptr, this will be a prekey message
		Expects((((public_identity_key != nullptr) && (public_prekey != nullptr))
					|| ((public_prekey == nullptr) && (public_identity_key == nullptr)))
				&& ((public_identity_key == nullptr) || !public_identity_key->empty)
				&& ((public_prekey == nullptr) || !public_prekey->empty));

		HeaderKey send_header_key;
		MessageKey send_message_key;
		PublicKey send_ephemeral_key;
		uint32_t send_message_number;
		uint32_t previous_send_message_number;
		this->ratchet_pointer->send(
				send_header_key,
				send_message_number,
				previous_send_message_number,
				send_ephemeral_key,
				send_message_key);

		auto header{header_construct(
				send_ephemeral_key,
				send_message_number,
				previous_send_message_number)};

		auto packet_type{molch_message_type::NORMAL_MESSAGE};
		//check if this is a prekey message
		if (public_identity_key != nullptr) {
			packet_type = molch_message_type::PREKEY_MESSAGE;
		}

		return packet_encrypt(
				packet_type,
				header,
				send_header_key,
				message,
				send_message_key,
				public_identity_key,
				public_ephemeral_key,
				public_prekey);
	}

	/*
	 * Try to decrypt a packet with skipped over header and message keys.
	 * This corresponds to "try_skipped_header_and_message_keys" from the
	 * Axolotl protocol description.
	 *
	 * Returns 0, if it was able to decrypt the packet.
	 */
	int Conversation::trySkippedHeaderAndMessageKeys(
			const span<const std::byte> packet,
			Buffer& message,
			uint32_t& receive_message_number,
			uint32_t& previous_receive_message_number) {
		//create buffers

		for (size_t index{0}; index < this->ratchet_pointer->skipped_header_and_message_keys.keys().size(); index++) {
			auto& node = this->ratchet_pointer->skipped_header_and_message_keys.keys()[index];
			std::optional<Buffer> header;
			std::optional<Buffer> message_optional;
			uint32_t current_protocol_version;
			uint32_t highest_supported_protocol_version;
			molch_message_type packet_type;
			packet_decrypt(
					current_protocol_version,
					highest_supported_protocol_version,
					packet_type,
					header,
					message_optional,
					packet,
					node.headerKey(),
					node.messageKey(),
					nullptr,
					nullptr,
					nullptr);
			if (header && message_optional) {
				message = std::move(*message_optional);
				this->ratchet_pointer->skipped_header_and_message_keys.remove(index);

				PublicKey their_signed_public_ephemeral;
				header_extract(
						their_signed_public_ephemeral,
						receive_message_number,
						previous_receive_message_number,
						*header);
				return static_cast<int>(status_type::SUCCESS);
			}
		}

		return static_cast<int>(status_type::NOT_FOUND);
	}

	/*
	 * Receive and decrypt a message using an existing conversation.
	 *
	 * Don't forget to destroy the return status with return_status_destroy_errors()
	 * if an error has occurred.
	 */
	Buffer Conversation::receive(
			const span<const std::byte> packet, //received packet
			uint32_t& receive_message_number,
			uint32_t& previous_receive_message_number) {
		try {
			Buffer message;
			auto status{trySkippedHeaderAndMessageKeys(
					packet,
					message,
					receive_message_number,
					previous_receive_message_number)};
			if (status == static_cast<int>(status_type::SUCCESS)) {
				// found a key and successfully decrypted the message
				return message;
			}

			HeaderKey current_receive_header_key;
			HeaderKey next_receive_header_key;
			this->ratchet_pointer->getReceiveHeaderKeys(current_receive_header_key, next_receive_header_key);

			//try to decrypt the packet header with the current receive header key
			auto header{packet_decrypt_header(packet, current_receive_header_key)};
			if (header) {
				this->ratchet_pointer->setHeaderDecryptability(Ratchet::HeaderDecryptability::CURRENT_DECRYPTABLE);
			} else {
				header = packet_decrypt_header(packet, next_receive_header_key);
				if (header) {
					this->ratchet_pointer->setHeaderDecryptability(Ratchet::HeaderDecryptability::NEXT_DECRYPTABLE);
				} else {
					this->ratchet_pointer->setHeaderDecryptability(Ratchet::HeaderDecryptability::UNDECRYPTABLE);
					throw Exception{status_type::DECRYPT_ERROR, "Failed to decrypt the message."};
				}
			}

			//extract data from the header
			PublicKey their_signed_public_ephemeral;
			uint32_t local_receive_message_number;
			uint32_t local_previous_receive_message_number;
			header_extract(
					their_signed_public_ephemeral,
					local_receive_message_number,
					local_previous_receive_message_number,
					*header);

			//and now decrypt the message with the message key
			//now we have all the data we need to advance the ratchet
			//so let's do that
			MessageKey message_key;
			this->ratchet_pointer->receive(
				message_key,
				their_signed_public_ephemeral,
				local_receive_message_number,
				local_previous_receive_message_number);

			std::optional<Buffer> message_optional{packet_decrypt_message(packet, message_key)};
			if (!message_optional) {
				throw Exception{status_type::DECRYPT_ERROR, "Failed to decrypt the message."};
			}
			message = std::move(*message_optional);

			this->ratchet_pointer->setLastMessageAuthenticity(true);

			receive_message_number = local_receive_message_number;
			previous_receive_message_number = local_previous_receive_message_number;

			return message;
		} catch (const std::exception&) {
			this->ratchet_pointer->setLastMessageAuthenticity(false);
			throw;
		}
	}

	ProtobufCConversation* Conversation::exportProtobuf(Arena& pool) const {
		//export the ratchet
		auto exported_conversation{this->ratchet_pointer->exportProtobuf(pool)};

		//export the conversation id
		auto id{pool.allocate<std::byte>(CONVERSATION_ID_SIZE)};
		this->id_storage.copyTo({id, CONVERSATION_ID_SIZE});
		exported_conversation->id.data = byte_to_uchar(id);
		exported_conversation->id.len = CONVERSATION_ID_SIZE;

		return exported_conversation;
	}

	Conversation::Conversation(const ProtobufCConversation& conversation_protobuf) {
		//copy the id
		this->id_storage.set({
				uchar_to_byte(conversation_protobuf.id.data),
				conversation_protobuf.id.len});

		//import the ratchet
		this->ratchet_pointer = std::make_unique<Ratchet>(conversation_protobuf);
	}

	const Key<CONVERSATION_ID_SIZE,KeyType::Key>& Conversation::id() const {
		return this->id_storage;
	}

	Ratchet& Conversation::ratchet() {
		if (!this->ratchet_pointer) {
			throw Exception{status_type::INCORRECT_DATA, "The ratchet doesn't point to anything."};
		}

		return *this->ratchet_pointer;
	}

	std::ostream& Conversation::print(std::ostream& stream) const {
		stream << "Conversation-ID:\n";
		this->id_storage.printHex(stream) << "\n";

		return stream;
	}
}

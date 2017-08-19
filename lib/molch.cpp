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

/*
 * WARNING: ALTHOUGH THIS IMPLEMENTS THE AXOLOTL PROTOCOL, IT ISN't CONSIDERED SECURE ENOUGH TO USE AT THIS POINT
 */

#include <cstdint>
#include <memory>

#include "constants.h"
#include "molch.h"
#include "packet.hpp"
#include "buffer.hpp"
#include "user-store.hpp"
#include "endianness.hpp"
#include "zeroed_malloc.hpp"
#include "destroyers.hpp"
#include "malloc.hpp"

extern "C" {
	#include <encrypted_backup.pb-c.h>
	#include <backup.pb-c.h>
}

class GlobalBackupDeleter {
public:
	void operator()(Buffer* buffer) {
		if (buffer != nullptr) {
			sodium_mprotect_readwrite(buffer->content);
			delete buffer;
		}
	}
};


//global user store
static std::unique_ptr<UserStore> users;
static std::unique_ptr<Buffer,GlobalBackupDeleter> global_backup_key;

class GlobalBackupKeyUnlocker {
public:
	GlobalBackupKeyUnlocker() {
		if (!global_backup_key) {
			throw MolchException(GENERIC_ERROR, "No backup key to unlock!");
		}
		sodium_mprotect_readonly(global_backup_key->content);
	}

	~GlobalBackupKeyUnlocker() {
		sodium_mprotect_noaccess(global_backup_key->content);
	}
};

class GlobalBackupKeyWriteUnlocker {
public:
	GlobalBackupKeyWriteUnlocker() {
		if (!global_backup_key) {
			throw MolchException(GENERIC_ERROR, "No backup key to unlock!");
		}
		sodium_mprotect_readwrite(global_backup_key->content);
	}

	~GlobalBackupKeyWriteUnlocker() {
		sodium_mprotect_noaccess(global_backup_key->content);
	}
};

/*
 * Create a prekey list.
 */
static Buffer create_prekey_list(const Buffer& public_signing_key) {
	//get the user
	auto user = users->find(public_signing_key);
	if (user == nullptr) {
		throw MolchException(NOT_FOUND, "Couldn't find the user to create a prekey list from.");
	}

	//rotate the prekeys
	user->prekeys.rotate();

	//get the public identity key
	Buffer public_identity_key(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE, &malloc, &free);
	user->master_keys.getIdentityKey(public_identity_key);

	//copy the public identity to the prekey list
	Buffer unsigned_prekey_list(
			PUBLIC_KEY_SIZE + PREKEY_AMOUNT * PUBLIC_KEY_SIZE + sizeof(uint64_t),
			0,
			&malloc,
			&free);
	unsigned_prekey_list.copyFrom(0, public_identity_key, 0, PUBLIC_KEY_SIZE);

	//get the prekeys
	Buffer prekeys(unsigned_prekey_list.content + PUBLIC_KEY_SIZE, PREKEY_AMOUNT * PUBLIC_KEY_SIZE);
	user->prekeys.list(prekeys);

	//add the expiration date
	int64_t expiration_date = time(nullptr) + 3600 * 24 * 31 * 3; //the prekey list will expire in 3 months
	Buffer big_endian_expiration_date(unsigned_prekey_list.content + PUBLIC_KEY_SIZE + PREKEY_AMOUNT * PUBLIC_KEY_SIZE, sizeof(int64_t));
	to_big_endian(expiration_date, big_endian_expiration_date);
	unsigned_prekey_list.size = unsigned_prekey_list.capacity();

	//sign the prekey list with the current identity key
	Buffer prekey_list(
			PUBLIC_KEY_SIZE + PREKEY_AMOUNT * PUBLIC_KEY_SIZE + sizeof(uint64_t) + SIGNATURE_SIZE,
			0,
			&malloc,
			&free);
	user->master_keys.sign(unsigned_prekey_list, prekey_list);

	return prekey_list;
}

/*
 * Create a new user. The user is identified by the public master key.
 *
 * Get's random input (can be in any format and doesn't have
 * to be uniformly distributed) and uses it in combination
 * with the OS's random number generator to generate a
 * signing and identity keypair for the user.
 *
 * IMPORTANT: Don't put random numbers provided by the operating
 * system in there.
 *
 * This also creates a signed list of prekeys to be uploaded to
 * the server.
 *
 * A new backup key is generated that subsequent backups of the library state will be encrypted with.
 *
 * Don't forget to destroy the return status with molch_destroy_return_status()
 * if an error has occurred.
 */
return_status molch_create_user(
		//outputs
		unsigned char *const public_master_key, //PUBLIC_MASTER_KEY_SIZE
		const size_t public_master_key_length,
		unsigned char **const prekey_list, //needs to be freed
		size_t *const prekey_list_length,
		unsigned char * backup_key, //BACKUP_KEY_SIZE
		const size_t backup_key_length,
		//optional output (can be nullptr)
		unsigned char **const backup, //exports the entire library state, free after use, check if nullptr before use!
		size_t *const backup_length,
		//optional input (can be nullptr)
		const unsigned char *const random_data,
		const size_t random_data_length) {
	return_status status = return_status_init();

	bool user_store_created = false;

	try {
		if ((public_master_key == nullptr)
			|| (prekey_list == nullptr) || (prekey_list_length == nullptr)) {
			throw MolchException(INVALID_INPUT, "Invalid input to molch_create_user.");
		}

		if (backup_key_length != BACKUP_KEY_SIZE) {
			throw MolchException(INCORRECT_BUFFER_SIZE, "Backup key has incorrect length.");
		}

		if (public_master_key_length != PUBLIC_MASTER_KEY_SIZE) {
			throw MolchException(INCORRECT_BUFFER_SIZE, "Public master key has incorrect length.");
		}

		//initialise libsodium and create user store
		if (!users) {
			if (sodium_init() == -1) {
				throw MolchException(INIT_ERROR, "Failed to init libsodium.");
			}
			users = std::make_unique<UserStore>();
		}

		//create a new backup key
		{
			return_status status = molch_update_backup_key(backup_key, backup_key_length);
			on_error {
				throw MolchException(status);
			}
		}

		//create the user
		Buffer random_data_buffer(random_data, random_data_length);
		Buffer public_master_key_buffer(public_master_key, PUBLIC_MASTER_KEY_SIZE);
		if (random_data_length != 0) {
			users->add(UserStoreNode(random_data_buffer, &public_master_key_buffer));
		} else {
			users->add(UserStoreNode(&public_master_key_buffer));
		}

		user_store_created = true;

		auto prekey_list_buffer = create_prekey_list(public_master_key_buffer);

		if (backup != nullptr) {
			*backup = nullptr;
			if (backup_length != nullptr) {
				return_status status = molch_export(backup, backup_length);
				on_error {
					throw MolchException(status);
				}
			}
		}

		//move the prekey list out of the buffer
		*prekey_list_length = prekey_list_buffer.size;
		*prekey_list = prekey_list_buffer.release();
	} catch (const MolchException& exception) {
		status = exception.toReturnStatus();
		goto cleanup;
	} catch (const std::exception& exception) {
		THROW(EXCEPTION, exception.what());
	}

cleanup:
	on_error {
		if (user_store_created) {
			return_status new_status = molch_destroy_user(public_master_key, public_master_key_length, nullptr, nullptr);
			return_status_destroy_errors(&new_status);
		}
	}

	return status;
}

/*
 * Destroy a user.
 *
 * Don't forget to destroy the return status with return_status_destroy_errors()
 * if an error has occurred.
 */
return_status molch_destroy_user(
		const unsigned char *const public_master_key,
		const size_t public_master_key_length,
		//optional output (can be nullptr)
		unsigned char **const backup, //exports the entire library state, free after use, check if nullptr before use!
		size_t *const backup_length
) {
	return_status status = return_status_init();

	try {
		if (!users) {
			throw MolchException(INIT_ERROR, "Molch hasn't been initialised yet.");
		}

		if (public_master_key_length != PUBLIC_MASTER_KEY_SIZE) {
			throw MolchException(INCORRECT_BUFFER_SIZE, "Public master key has incorrect size.");
		}

		Buffer public_signing_key_buffer(public_master_key, PUBLIC_MASTER_KEY_SIZE);
		users->remove(public_signing_key_buffer);

		if (backup != nullptr) {
			*backup = nullptr;
			if (backup_length != nullptr) {
				return_status status = molch_export(backup, backup_length);
				on_error {
					throw MolchException(status);
				}
			}
		}
	} catch (const MolchException& exception) {
		status = exception.toReturnStatus();
		goto cleanup;
	} catch (const std::exception& exception) {
		THROW(EXCEPTION, exception.what());
	}

cleanup:
	return status;
}

/*
 * Get the number of users.
 */
size_t molch_user_count() {
	if (!users) {
		return 0;
	}

	return users->size();
}

/*
 * Delete all users.
 */
void molch_destroy_all_users() {
	if (users) {
		users->clear();
	}
}

/*
 * List all of the users (list of the public keys),
 * nullptr if there are no users.
 *
 * The list is accessible via the return status' 'data' property.
 *
 * Don't forget to destroy the return status with return_status_destroy_errors()
 * if an error has occurred.
 */
return_status molch_list_users(
		unsigned char **const user_list,
		size_t * const user_list_length, //length in bytes
		size_t * const count) {
	return_status status = return_status_init();

	try {
		if (!users || (user_list_length == nullptr)) {
			throw MolchException(INVALID_INPUT, "Invalid input to molch_list_users.");
		}

		//get the list of users and copy it
		auto list = users->list();

		*count = molch_user_count();

		if (*count == 0) {
			*user_list = nullptr;
		} else {
			*user_list = throwing_malloc<unsigned char>(*count * PUBLIC_MASTER_KEY_SIZE);
			std::copy(list.content, list.content + list.size, *user_list);
		}

		*user_list_length = list.size;
	} catch (const MolchException& exception) {
		status = exception.toReturnStatus();
		goto cleanup;
	}

cleanup:
	on_error {
		if (user_list != nullptr) {
			free_and_null_if_valid(*user_list);
		}

		*count = 0;
	}

	return status;
}

/*
 * Get the type of a message.
 *
 * This is either a normal message or a prekey message.
 * Prekey messages mark the start of a new conversation.
 */
molch_message_type molch_get_message_type(
		const unsigned char * const packet,
		const size_t packet_length) {

	molch_message_type packet_type;

	try {
		//create a buffer for the packet
		Buffer packet_buffer(packet, packet_length);

		uint32_t current_protocol_version;
		uint32_t highest_supported_protocol_version;
		packet_get_metadata_without_verification(
			current_protocol_version,
			highest_supported_protocol_version,
			packet_type,
			packet_buffer,
			nullptr,
			nullptr,
			nullptr);
	} catch (const std::exception& exception) {
		return INVALID;
	}

	return packet_type;
}

/*
 * Verify prekey list and extract the public identity
 * and choose a prekey.
 */
static void verify_prekey_list(
		const unsigned char * const prekey_list,
		const size_t prekey_list_length,
		Buffer& public_identity_key, //output, PUBLIC_KEY_SIZE
		Buffer& public_signing_key) {
	//verify the signature
	Buffer verified_prekey_list(prekey_list_length - SIGNATURE_SIZE, prekey_list_length - SIGNATURE_SIZE);
	unsigned long long verified_length;
	int status = crypto_sign_open(
			verified_prekey_list.content,
			&verified_length,
			prekey_list,
			static_cast<unsigned long long>(prekey_list_length),
			public_signing_key.content);
	if (status != 0) {
		throw MolchException(VERIFICATION_FAILED, "Failed to verify prekey list signature.");
	}
	if (verified_length > SIZE_MAX)
	{
		throw MolchException(CONVERSION_ERROR, "Length is bigger than size_t.");
	}
	verified_prekey_list.size = static_cast<size_t>(verified_length);

	//get the expiration date
	int64_t expiration_date;
	Buffer big_endian_expiration_date(verified_prekey_list.content + PUBLIC_KEY_SIZE + PREKEY_AMOUNT * PUBLIC_KEY_SIZE, sizeof(int64_t));
	from_big_endian(expiration_date, big_endian_expiration_date);

	//make sure the prekey list isn't too old
	int64_t current_time = time(nullptr);
	if (expiration_date < current_time) {
		throw MolchException(OUTDATED, "Prekey list has expired (older than 3 months).");
	}

	//copy the public identity key
	public_identity_key.copyFrom(0, verified_prekey_list, 0, PUBLIC_KEY_SIZE);
}

/*
 * Start a new conversation. (sending)
 *
 * The conversation can be identified by it's ID
 *
 * This requires a new set of prekeys from the receiver.
 *
 *
 * Don't forget to destroy the return status with return_status_destroy_errors()
 * if an error has occurred.
 */
return_status molch_start_send_conversation(
		//outputs
		unsigned char *const conversation_id, //CONVERSATION_ID_SIZE long (from conversation.h)
		const size_t conversation_id_length,
		unsigned char **const packet, //free after use
		size_t *packet_length,
		//inputs
		const unsigned char *const sender_public_master_key, //signing key of the sender (user)
		const size_t sender_public_master_key_length,
		const unsigned char *const receiver_public_master_key, //signing key of the receiver
		const size_t receiver_public_master_key_length,
		const unsigned char *const prekey_list, //prekey list of the receiver
		const size_t prekey_list_length,
		const unsigned char *const message,
		const size_t message_length,
		//optional output (can be nullptr)
		unsigned char **const backup, //exports the entire library state, free after use, check if nullptr before use!
		size_t *const backup_length
) {
	return_status status = return_status_init();

	try {
		if (!users) {
			throw MolchException(INIT_ERROR, "Molch hasn't been initialised yet.");
		}

		//check input
		if ((conversation_id == nullptr)
				|| (packet == nullptr)
				|| (packet_length == nullptr)
				|| (prekey_list == nullptr)
				|| (sender_public_master_key == nullptr)
				|| (receiver_public_master_key == nullptr)) {
			throw MolchException(INVALID_INPUT, "Invalid input to molch_start_send_conversation.");
		}

		if (conversation_id_length != CONVERSATION_ID_SIZE) {
			throw MolchException(INCORRECT_BUFFER_SIZE, "conversation id has incorrect size.");
		}

		if (sender_public_master_key_length != PUBLIC_MASTER_KEY_SIZE) {
			throw MolchException(INCORRECT_BUFFER_SIZE, "sender public master key has incorrect size.");
		}

		if (receiver_public_master_key_length != PUBLIC_MASTER_KEY_SIZE) {
			throw MolchException(INCORRECT_BUFFER_SIZE, "receiver public master key has incorrect size.");
		}

		//get the user that matches the public signing key of the sender
		Buffer sender_public_master_key_buffer(sender_public_master_key, PUBLIC_MASTER_KEY_SIZE);
		UserStoreNode *user = users->find(sender_public_master_key_buffer);
		if (user == nullptr) {
			throw MolchException(NOT_FOUND, "User not found.");
		}

		//get the receivers public ephemeral and identity
		Buffer receiver_public_identity(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);
		Buffer receiver_public_master_key_buffer(receiver_public_master_key, PUBLIC_MASTER_KEY_SIZE);
		verify_prekey_list(
				prekey_list,
				prekey_list_length,
				receiver_public_identity,
				receiver_public_master_key_buffer);

		//unlock the master keys
		MasterKeys::Unlocker unlocker(user->master_keys);

		//create the conversation and encrypt the message
		Buffer prekeys(prekey_list + PUBLIC_KEY_SIZE + SIGNATURE_SIZE, prekey_list_length - PUBLIC_KEY_SIZE - SIGNATURE_SIZE - sizeof(int64_t));
		Buffer message_buffer(message, message_length);
		Buffer packet_buffer;
		ConversationT conversation(
				message_buffer,
				packet_buffer,
				user->master_keys.public_identity_key,
				user->master_keys.private_identity_key,
				receiver_public_identity,
				prekeys);

		//copy the conversation id
		Buffer conversation_id_buffer(conversation_id, CONVERSATION_ID_SIZE);
		conversation_id_buffer.cloneFrom(conversation.id);

		user->conversations.add(std::move(conversation));

		//copy the packet to a malloced buffer output
		Buffer malloced_packet(packet_buffer.size, 0, &malloc, &free);
		malloced_packet.cloneFrom(packet_buffer);

		if (backup != nullptr) {
			*backup = nullptr;
			if (backup_length != nullptr) {
				return_status status = molch_export(backup, backup_length);
				on_error {
					throw MolchException(status);
				}
			}
		}

		*packet_length = malloced_packet.size;
		*packet = malloced_packet.release();
	} catch (const MolchException& exception) {
		status = exception.toReturnStatus();
		goto cleanup;
	} catch (const std::exception& exception) {
		THROW(EXCEPTION, exception.what());
	}

cleanup:
	return status;
}

	/*
	 * Start a new conversation. (receiving)
	 *
	 * This also generates a new set of prekeys to be uploaded to the server.
	 *
	 * This function is called after receiving a prekey message.
	 *
	 * Don't forget to destroy the return status with return_status_destroy_errors()
	 * if an error has occurred.
	 */
	return_status molch_start_receive_conversation(
			//outputs
			unsigned char * const conversation_id, //CONVERSATION_ID_SIZE long (from conversation.h)
			const size_t conversation_id_length,
			unsigned char ** const prekey_list, //free after use
			size_t * const prekey_list_length,
			unsigned char ** const message, //free after use
			size_t * const message_length,
			//inputs
			const unsigned char * const receiver_public_master_key, //signing key of the receiver (user)
			const size_t receiver_public_master_key_length,
			const unsigned char * const sender_public_master_key, //signing key of the sender
			const size_t sender_public_master_key_length,
			const unsigned char * const packet, //received prekey packet
			const size_t packet_length,
			//optional output (can be nullptr)
			unsigned char ** const backup, //exports the entire library state, free after use, check if nullptr before use!
			size_t * const backup_length
			) {

		return_status status = return_status_init();

		try {
			if (!users) {
				throw MolchException(INIT_ERROR, "Molch hasn't been initialized yet.");
			}

			if ((conversation_id == nullptr)
				|| (message == nullptr) || (message_length == nullptr)
				|| (packet == nullptr)
				|| (prekey_list == nullptr) || (prekey_list_length == nullptr)
				|| (sender_public_master_key == nullptr)
				|| (receiver_public_master_key == nullptr)) {
				throw MolchException(INVALID_INPUT, "Invalid input to molch_start_receive_conversation.");
			}

			if (conversation_id_length != CONVERSATION_ID_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "Conversation ID has an incorrect size.");
			}

			if (sender_public_master_key_length != PUBLIC_MASTER_KEY_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "Senders public master key has an incorrect size.");
			}

			if (receiver_public_master_key_length != PUBLIC_MASTER_KEY_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "Receivers public master key has an incorrect size.");
			}

			//get the user that matches the public signing key of the receiver
			Buffer receiver_public_master_key_buffer(receiver_public_master_key, PUBLIC_MASTER_KEY_SIZE);
			UserStoreNode *user = users->find(receiver_public_master_key_buffer);
			if (user == nullptr) {
				throw MolchException(NOT_FOUND, "User not found in the user store.");
			}

			//unlock the master keys
			MasterKeys::Unlocker unlocker(user->master_keys);

			//create the conversation
			Buffer packet_buffer(packet, packet_length);
			Buffer message_buffer;
			ConversationT conversation(
					packet_buffer,
					message_buffer,
					user->master_keys.public_identity_key,
					user->master_keys.private_identity_key,
					user->prekeys);

			//copy the conversation id
			Buffer conversation_id_buffer(conversation_id, CONVERSATION_ID_SIZE);
			conversation_id_buffer.cloneFrom(conversation.id);

			//create the prekey list
			auto prekey_list_buffer = create_prekey_list(receiver_public_master_key_buffer);

			//add the conversation to the conversation store
			user->conversations.add(std::move(conversation));

			//copy the message
			Buffer malloced_message(message_buffer.size, 0, &malloc, &free);
			malloced_message.cloneFrom(message_buffer);

			if (backup != nullptr) {
				*backup = nullptr;
				if (backup_length != nullptr) {
					return_status status = molch_export(backup, backup_length);
					on_error {
						throw MolchException(status);
					}
				}
			}

			*message_length = malloced_message.size;
			*message = malloced_message.release();

			*prekey_list_length = prekey_list_buffer.size;
			*prekey_list = prekey_list_buffer.release();
		} catch (const MolchException& exception) {
			status = exception.toReturnStatus();
			goto cleanup;
		} catch (const std::exception& exception) {
			THROW(EXCEPTION, exception.what());
		}

	cleanup:
		return status;
	}

	/*
	 * Encrypt a message and create a packet that can be sent to the receiver.
	 *
	 * Don't forget to destroy the return status with return_status_destroy_errors()
	 * if an error has occurred.
	 */
	return_status molch_encrypt_message(
			//output
			unsigned char ** const packet, //free after use
			size_t *packet_length,
			//inputs
			const unsigned char * const conversation_id,
			const size_t conversation_id_length,
			const unsigned char * const message,
			const size_t message_length,
			//optional output (can be nullptr)
			unsigned char ** const conversation_backup, //exports the conversation, free after use, check if nullptr before use!
			size_t * const conversation_backup_length
			) {
		return_status status = return_status_init();

		try {
			if (!users) {
				throw MolchException(INIT_ERROR, "Molch hasn't been initialized yet.");
			}

			if ((packet == nullptr) || (packet_length == nullptr)
				|| (message == nullptr)
				|| (conversation_id == nullptr)) {
				throw MolchException(INVALID_INPUT, "Invalid input to molch_encrypt_message.");
			}

			if (conversation_id_length != CONVERSATION_ID_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "Conversation ID has an incorrect size.");
			}

			//find the conversation
			Buffer conversation_id_buffer(conversation_id, CONVERSATION_ID_SIZE);
			UserStoreNode *user;
			auto *conversation = users->findConversation(user, conversation_id_buffer);
			if (conversation == nullptr) {
				throw MolchException(NOT_FOUND, "Failed to find a conversation for the given ID.");
			}

			Buffer message_buffer(message, message_length);
			auto packet_buffer = conversation->send(
					message_buffer,
					nullptr,
					nullptr,
					nullptr);

			//copy the packet content
			Buffer malloced_packet(packet_buffer.size, 0, &malloc, &free);
			malloced_packet.cloneFrom(packet_buffer);

			if (conversation_backup != nullptr) {
				*conversation_backup = nullptr;
				if (conversation_backup_length != nullptr) {
					return_status status = molch_conversation_export(conversation_backup, conversation_backup_length, conversation->id.content, conversation->id.size);
					on_error {
						throw MolchException(status);
					}
				}
			}

			*packet_length = malloced_packet.size;
			*packet = malloced_packet.release();
		} catch (const MolchException& exception) {
			status = exception.toReturnStatus();
			goto cleanup;
		} catch (const std::exception& exception) {
			THROW(EXCEPTION, exception.what());
		}

	cleanup:
		return status;
	}

	/*
	 * Decrypt a message.
	 *
	 * Don't forget to destroy the return status with return_status_destroy_errors()
	 * if an error has occurred.
	 */
	return_status molch_decrypt_message(
			//outputs
			unsigned char ** const message, //free after use
			size_t *message_length,
			uint32_t * const receive_message_number,
			uint32_t * const previous_receive_message_number,
			//inputs
			const unsigned char * const conversation_id,
			const size_t conversation_id_length,
			const unsigned char * const packet,
			const size_t packet_length,
			//optional output (can be nullptr)
			unsigned char ** const conversation_backup, //exports the conversation, free after use, check if nullptr before use!
			size_t * const conversation_backup_length
		) {
		return_status status = return_status_init();

		try {
			if (!users) {
				throw MolchException(INIT_ERROR, "Molch hasn't been initialized yet.");
			}

			if ((message == nullptr) || (message_length == nullptr)
				|| (packet == nullptr)
				|| (conversation_id == nullptr)
				|| (receive_message_number == nullptr)
				|| (previous_receive_message_number == nullptr)) {
				throw MolchException(INVALID_INPUT, "Invalid input to molch_decrypt_message.");
			}

			if (conversation_id_length != CONVERSATION_ID_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "Conversation ID has an incorrect size.");
			}

			//find the conversation
			Buffer conversation_id_buffer(conversation_id, CONVERSATION_ID_SIZE);
			UserStoreNode* user;
			auto conversation = users->findConversation(user, conversation_id_buffer);
			if (conversation == nullptr) {
				throw MolchException(NOT_FOUND, "Failed to find conversation with the given ID.");
			}

			Buffer packet_buffer(packet, packet_length);
			auto message_buffer = conversation->receive(
					packet_buffer,
					*receive_message_number,
					*previous_receive_message_number);

			//copy the message
			Buffer malloced_message(message_buffer.size, 0, &malloc, &free);
			malloced_message.cloneFrom(message_buffer);

			if (conversation_backup != nullptr) {
				*conversation_backup = nullptr;
				if (conversation_backup_length != nullptr) {
					return_status status = molch_conversation_export(conversation_backup, conversation_backup_length, conversation->id.content, conversation->id.size);
					on_error {
						throw MolchException(status);
					}
				}
			}

			*message_length = malloced_message.size;
			*message = malloced_message.release();
		} catch (const MolchException& exception) {
			status = exception.toReturnStatus();
			goto cleanup;
		} catch (const std::exception& exception) {
			THROW(EXCEPTION, exception.what());
		}

	cleanup:
		return status;
	}

	return_status molch_end_conversation(
			//input
			const unsigned char * const conversation_id,
			const size_t conversation_id_length,
			//optional output (can be nullptr)
			unsigned char ** const backup,
			size_t * const backup_length
			) {
		return_status status = return_status_init();

		try {
			if (!users) {
				throw MolchException(INIT_ERROR, "Molch hasn't been initialized yet.");
			}

			if (conversation_id == nullptr) {
				throw MolchException(INVALID_INPUT, "Invalid input to molch_end_conversation.");
			}

			if (conversation_id_length != CONVERSATION_ID_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "Conversation ID has an incorrect length.");
			}

			//find the conversation
			UserStoreNode *user = nullptr;
			Buffer conversation_id_buffer(conversation_id, CONVERSATION_ID_SIZE);
			auto conversation = users->findConversation(user, conversation_id_buffer);
			if (conversation == nullptr) {
				throw MolchException(NOT_FOUND, "Couldn't find conversation.");
			}

			user->conversations.remove(conversation_id_buffer);

			if (backup != nullptr) {
				*backup = nullptr;
				if (backup_length != nullptr) {
					return_status status = molch_export(backup, backup_length);
					on_error {
						throw MolchException(status);
					}
				}
			}
		} catch (const MolchException& exception) {
			status = exception.toReturnStatus();
			goto cleanup;
		} catch (const std::exception& exception) {
			THROW(EXCEPTION, exception.what());
		}

	cleanup:
		return status;
	}

	/*
	 * List the conversations of a user.
	 *
	 * Returns the number of conversations and a list of conversations for a given user.
	 * (all the conversation ids in one big list).
	 *
	 * Don't forget to free conversation_list after use.
	 *
	 * Don't forget to destroy the return status with return_status_destroy_errors()
	 * if an error has occurred.
	 */
	return_status molch_list_conversations(
			//outputs
			unsigned char ** const conversation_list,
			size_t * const conversation_list_length,
			size_t * const number,
			//inputs
			const unsigned char * const user_public_master_key,
			const size_t user_public_master_key_length) {
		return_status status = return_status_init();

		try {
			if (conversation_list != nullptr) {
				*conversation_list = nullptr;
			}

			if (!users) {
				throw MolchException(INIT_ERROR, "Molch hasn't been initialized yet.");
			}

			if ((user_public_master_key == nullptr) || (conversation_list == nullptr) || (conversation_list_length == nullptr) || (number == nullptr)) {
				throw MolchException(INVALID_INPUT, "Invalid input to molch_list_conversations.");
			}

			if (user_public_master_key_length != PUBLIC_MASTER_KEY_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "Public master key has an incorrect length.");
			}

			Buffer user_public_master_key_buffer(user_public_master_key, PUBLIC_MASTER_KEY_SIZE);
			auto user = users->find(user_public_master_key_buffer);
			if (user == nullptr) {
				throw MolchException(NOT_FOUND, "No user found for the given public identity.");
			}

			auto conversation_list_buffer = user->conversations.list();
			if (conversation_list_buffer.isNone()) {
				// list is empty
				*conversation_list = nullptr;
				*number = 0;
			} else {
				if ((conversation_list_buffer.size % CONVERSATION_ID_SIZE) != 0) {
					throw MolchException(INCORRECT_BUFFER_SIZE, "The conversation ID buffer has an incorrect length.");
				}
				*number = conversation_list_buffer.size / CONVERSATION_ID_SIZE;

				//allocate the conversation list output and copy it over
				Buffer malloced_conversation_list(conversation_list_buffer.size, 0, &malloc, &free);
				malloced_conversation_list.cloneFrom(conversation_list_buffer);
				*conversation_list_length = malloced_conversation_list.size;
				*conversation_list = malloced_conversation_list.release();
			}
		} catch (const MolchException& exception) {
			status = exception.toReturnStatus();
			goto cleanup;
		} catch (const std::exception& exception) {
			THROW(EXCEPTION, exception.what());
		}

	cleanup:
		on_error {
			if (number != nullptr) {
				*number = 0;
			}

			if (conversation_list_length != nullptr) {
				*conversation_list_length = 0;
			}
		}

		return status;
	}

	/*
	 * Print a return status into a nice looking error message.
	 *
	 * Don't forget to free the output after use.
	 */
	char *molch_print_status(size_t * const output_length, return_status status) {
		return return_status_print(&status, output_length);
	}

	/*
	 * Get a string describing the return status type.
	 *
	 * (return_status.status)
	 */
	const char *molch_print_status_type(status_type type) {
		return return_status_get_name(type);
	}

	/*
	 * Destroy a return status (only needs to be called if there was an error).
	 */
	void molch_destroy_return_status(return_status * const status) {
		return_status_destroy_errors(status);
	}

	/*
	 * Serialize a conversation.
	 *
	 * Don't forget to free the output after use.
	 *
	 * Don't forget to destroy the return status with molch_destroy_return_status()
	 * if an error has occurred.
	 */
	return_status molch_conversation_export(
			//output
			unsigned char ** const backup,
			size_t * const backup_length,
			//input
			const unsigned char * const conversation_id,
			const size_t conversation_id_length) {
		return_status status = return_status_init();

		try {
			EncryptedBackup encrypted_backup_struct;
			encrypted_backup__init(&encrypted_backup_struct);

			if (!users) {
				throw MolchException(INIT_ERROR, "Molch hasn't been initialized yet.");
			}

			//check input
			if ((backup == nullptr) || (backup_length == nullptr)
					|| (conversation_id == nullptr)) {
				throw MolchException(INVALID_INPUT, "Invalid input to molch_conversation_export");
			}
			if ((conversation_id_length != CONVERSATION_ID_SIZE)) {
				throw MolchException(INVALID_INPUT, "Conversation ID has an invalid size.");
			}

			if ((global_backup_key == nullptr) || (global_backup_key->size != BACKUP_KEY_SIZE)) {
				throw MolchException(INCORRECT_DATA, "No backup key found.");
			}

			//find the conversation
			UserStoreNode *user;
			Buffer conversation_id_buffer(conversation_id, CONVERSATION_ID_SIZE);
			auto conversation = users->findConversation(user, conversation_id_buffer);
			if (conversation == nullptr) {
				throw MolchException(NOT_FOUND, "Failed to find the conversation.");
			}

			//export the conversation
			auto conversation_struct = conversation->exportProtobuf();

			//pack the struct
			auto conversation_size = conversation__get_packed_size(conversation_struct.get());
			Buffer conversation_buffer(conversation_size, 0, &zeroed_malloc, &zeroed_free);

			conversation_buffer.size = conversation__pack(conversation_struct.get(), conversation_buffer.content);
			if (conversation_buffer.size != conversation_size) {
				throw MolchException(PROTOBUF_PACK_ERROR, "Failed to pack conversation to protobuf-c.");
			}

			//generate the nonce
			Buffer backup_nonce(BACKUP_NONCE_SIZE, 0);
			backup_nonce.fillRandom(BACKUP_NONCE_SIZE);

			//allocate the output
			Buffer backup_buffer(conversation_size + crypto_secretbox_MACBYTES, conversation_size + crypto_secretbox_MACBYTES);

			//encrypt the backup
			GlobalBackupKeyUnlocker unlocker;
			int status = crypto_secretbox_easy(
					backup_buffer.content,
					conversation_buffer.content,
					conversation_buffer.size,
					backup_nonce.content,
					global_backup_key->content);
			if (status != 0) {
				backup_buffer.size = 0;
				throw MolchException(ENCRYPT_ERROR, "Failed to enrypt conversation state.");
			}

			//fill in the encrypted backup struct
			//metadata
			encrypted_backup_struct.backup_version = 0;
			encrypted_backup_struct.has_backup_type = true;
			encrypted_backup_struct.backup_type = ENCRYPTED_BACKUP__BACKUP_TYPE__CONVERSATION_BACKUP;
			//nonce
			encrypted_backup_struct.has_encrypted_backup_nonce = true;
			encrypted_backup_struct.encrypted_backup_nonce.data = backup_nonce.content;
			encrypted_backup_struct.encrypted_backup_nonce.len = backup_nonce.size;
			//encrypted backup
			encrypted_backup_struct.has_encrypted_backup = true;
			encrypted_backup_struct.encrypted_backup.data = backup_buffer.content;
			encrypted_backup_struct.encrypted_backup.len = backup_buffer.size;

			//now pack the entire backup
			const size_t encrypted_backup_size = encrypted_backup__get_packed_size(&encrypted_backup_struct);
			Buffer malloced_encrypted_backup(encrypted_backup_size, 0, &malloc, &free);
			malloced_encrypted_backup.size = encrypted_backup__pack(&encrypted_backup_struct, malloced_encrypted_backup.content);
			if (malloced_encrypted_backup.size != encrypted_backup_size) {
				throw MolchException(PROTOBUF_PACK_ERROR, "Failed to pack encrypted conversation.");
			}
			*backup_length = malloced_encrypted_backup.size;
			*backup = malloced_encrypted_backup.release();
		} catch (const MolchException& exception) {
			status = exception.toReturnStatus();
			goto cleanup;
		} catch (const std::exception& exception) {
			THROW(EXCEPTION, exception.what());
		}

	cleanup:
		on_error {
			if ((backup != nullptr) && (*backup != nullptr)) {
				free(*backup);
				*backup = nullptr;
			}
			if (backup_length != nullptr) {
				*backup_length = 0;
			}
		}

		return status;
	}

	/*
	 * Import a conversation from a backup (overwrites the current one if it exists).
	 *
	 * Don't forget to destroy the return status with molch_destroy_return_status()
	 * if an error has occurred.
	 */
	return_status molch_conversation_import(
			//output
			unsigned char * new_backup_key,
			const size_t new_backup_key_length,
			//inputs
			const unsigned char * const backup,
			const size_t backup_length,
			const unsigned char * backup_key,
			const size_t backup_key_length) {
		return_status status = return_status_init();

		try {
			if (!users) {
				throw MolchException(INIT_ERROR, "Molch hasn't been initialized yet.");
			}

			//check input
			if ((backup == nullptr) || (backup_key == nullptr)) {
				throw MolchException(INVALID_INPUT, "Invalid input to molch_import.");
			}
			if (backup_key_length != BACKUP_KEY_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "Backup key has an incorrect length.");
			}
			if (new_backup_key_length != BACKUP_KEY_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "New backup key has an incorrect length.");
			}

			//unpack the encrypted backup
			auto encrypted_backup_struct = std::unique_ptr<EncryptedBackup,EncryptedBackupDeleter>(encrypted_backup__unpack(&protobuf_c_allocators, backup_length, backup));
			if (encrypted_backup_struct == nullptr) {
				throw MolchException(PROTOBUF_UNPACK_ERROR, "Failed to unpack encrypted backup from protobuf.");
			}

			//check the backup
			if (encrypted_backup_struct->backup_version != 0) {
				throw MolchException(INCORRECT_DATA, "Incompatible backup.");
			}
			if (!encrypted_backup_struct->has_backup_type || (encrypted_backup_struct->backup_type != ENCRYPTED_BACKUP__BACKUP_TYPE__CONVERSATION_BACKUP)) {
				throw MolchException(INCORRECT_DATA, "Backup is not a conversation backup.");
			}
			if (!encrypted_backup_struct->has_encrypted_backup || (encrypted_backup_struct->encrypted_backup.len < crypto_secretbox_MACBYTES)) {
				throw MolchException(PROTOBUF_MISSING_ERROR, "The backup is missing the encrypted conversation state.");
			}
			if (!encrypted_backup_struct->has_encrypted_backup_nonce || (encrypted_backup_struct->encrypted_backup_nonce.len != BACKUP_NONCE_SIZE)) {
				throw MolchException(PROTOBUF_MISSING_ERROR, "The backup is missing the nonce.");
			}

			Buffer decrypted_backup(
					encrypted_backup_struct->encrypted_backup.len - crypto_secretbox_MACBYTES,
					encrypted_backup_struct->encrypted_backup.len - crypto_secretbox_MACBYTES,
					&zeroed_malloc,
					&zeroed_free);

			//decrypt the backup
			int status_int = crypto_secretbox_open_easy(
					decrypted_backup.content,
					encrypted_backup_struct->encrypted_backup.data,
					encrypted_backup_struct->encrypted_backup.len,
					encrypted_backup_struct->encrypted_backup_nonce.data,
					backup_key);
			if (status_int != 0) {
				throw MolchException(DECRYPT_ERROR, "Failed to decrypt conversation backup.");
			}

			//unpack the struct
			auto conversation_struct = std::unique_ptr<Conversation,ConversationDeleter>(conversation__unpack(&protobuf_c_allocators, decrypted_backup.size, decrypted_backup.content));
			if (conversation_struct == nullptr) {
				throw MolchException(PROTOBUF_UNPACK_ERROR, "Failed to unpack conversations protobuf-c.");
			}

			//import the conversation
			ConversationT conversation(*conversation_struct);
			UserStoreNode* containing_user = nullptr;
			Buffer conversation_id_buffer(conversation_struct->id.data, conversation_struct->id.len);
			auto existing_conversation = users->findConversation(containing_user, conversation_id_buffer);
			if (existing_conversation == nullptr) {
				throw MolchException(NOT_FOUND, "Containing store not found.");
			}

			containing_user->conversations.add(std::move(conversation));

			//update the backup key
			return_status status = molch_update_backup_key(new_backup_key, new_backup_key_length);
			on_error {
				throw MolchException(KEYGENERATION_FAILED, "Failed to update backup key.");
			}
		} catch (const MolchException& exception) {
			status = exception.toReturnStatus();
			goto cleanup;
		} catch (const std::exception& exception) {
			THROW(EXCEPTION, exception.what());
		}

	cleanup:
		return status;
	}

	/*
	 * Serialise molch's internal state. The output is encrypted with the backup key.
	 *
	 * Don't forget to free the output after use.
	 *
	 * Don't forget to destroy the return status with molch_destroy_return_status()
	 * if an error has occured.
	 */
	return_status molch_export(
			unsigned char ** const backup,
			size_t *backup_length) {
		return_status status = return_status_init();

		try {
			if (!users) {
				throw MolchException(INIT_ERROR, "Molch hasn't been initialized yet.");
			}

			//check input
			if ((backup == nullptr) || (backup_length == nullptr)) {
				throw MolchException(INVALID_INPUT, "Invalid input to molch_export");
			}

			if ((global_backup_key == nullptr) || (global_backup_key->size != BACKUP_KEY_SIZE)) {
				throw MolchException(INCORRECT_DATA, "No backup key found.");
			}

			auto backup_struct = std::unique_ptr<Backup,BackupDeleter>(throwing_zeroed_malloc<Backup>(sizeof(Backup)));
			backup__init(backup_struct.get());

			//export the conversation
			users->exportProtobuf(backup_struct->users, backup_struct->n_users);

			//pack the struct
			auto backup_struct_size = backup__get_packed_size(backup_struct.get());
			Buffer users_buffer(backup_struct_size, 0, &zeroed_malloc, &zeroed_free);

			users_buffer.size = backup__pack(backup_struct.get(), users_buffer.content);
			if (users_buffer.size != backup_struct_size) {
				throw MolchException(PROTOBUF_PACK_ERROR, "Failed to pack conversation to protobuf-c.");
			}

			//generate the nonce
			Buffer backup_nonce(BACKUP_NONCE_SIZE, 0);
			backup_nonce.fillRandom(BACKUP_NONCE_SIZE);

			//allocate the output
			Buffer backup_buffer(backup_struct_size + crypto_secretbox_MACBYTES, backup_struct_size + crypto_secretbox_MACBYTES);

			//encrypt the backup
			GlobalBackupKeyUnlocker unlocker;
			int status = crypto_secretbox_easy(
					backup_buffer.content,
					users_buffer.content,
					users_buffer.size,
					backup_nonce.content,
					global_backup_key->content);
			if (status != 0) {
				throw MolchException(ENCRYPT_ERROR, "Failed to enrypt conversation state.");
			}

			//fill in the encrypted backup struct
			EncryptedBackup encrypted_backup_struct;
			encrypted_backup__init(&encrypted_backup_struct);
			//metadata
			encrypted_backup_struct.backup_version = 0;
			encrypted_backup_struct.has_backup_type = true;
			encrypted_backup_struct.backup_type = ENCRYPTED_BACKUP__BACKUP_TYPE__FULL_BACKUP;
			//nonce
			encrypted_backup_struct.has_encrypted_backup_nonce = true;
			encrypted_backup_struct.encrypted_backup_nonce.data = backup_nonce.content;
			encrypted_backup_struct.encrypted_backup_nonce.len = backup_nonce.size;
			//encrypted backup
			encrypted_backup_struct.has_encrypted_backup = true;
			encrypted_backup_struct.encrypted_backup.data = backup_buffer.content;
			encrypted_backup_struct.encrypted_backup.len = backup_buffer.size;

			//now pack the entire backup
			const size_t encrypted_backup_size = encrypted_backup__get_packed_size(&encrypted_backup_struct);
			Buffer malloced_encrypted_backup(encrypted_backup_size, 0, &malloc, &free);
			malloced_encrypted_backup.size = encrypted_backup__pack(&encrypted_backup_struct, malloced_encrypted_backup.content);
			if (malloced_encrypted_backup.size != encrypted_backup_size) {
				throw MolchException(PROTOBUF_PACK_ERROR, "Failed to pack encrypted conversation.");
			}
			*backup_length = malloced_encrypted_backup.size;
			*backup = malloced_encrypted_backup.release();
		} catch (const MolchException& exception) {
			status = exception.toReturnStatus();
			goto cleanup;
		} catch (const std::exception& exception) {
			THROW(EXCEPTION, exception.what());
		}

	cleanup:
		on_error {
			if ((backup != nullptr) && (*backup != nullptr)) {
				free(*backup);
				*backup = nullptr;
			}
			if (backup_length != nullptr) {
				*backup_length = 0;
			}
		}

		return status;
	}

	/*
	 * Import molch's internal state from a backup (overwrites the current state)
	 * and generates a new backup key.
	 *
	 * The backup key is needed to decrypt the backup.
	 *
	 * Don't forget to destroy the return status with molch_destroy_return_status()
	 * if an error has occured.
	 */
	return_status molch_import(
			//output
			unsigned char * const new_backup_key, //BACKUP_KEY_SIZE, can be the same pointer as the backup key
			const size_t new_backup_key_length,
			//inputs
			unsigned char * const backup,
			const size_t backup_length,
			const unsigned char * const backup_key, //BACKUP_KEY_SIZE
			const size_t backup_key_length
			) {
		return_status status = return_status_init();

		try {
			//check input
			if ((backup == nullptr) || (backup_key == nullptr)) {
				throw MolchException(INVALID_INPUT, "Invalid input to molch_import.");
			}
			if (backup_key_length != BACKUP_KEY_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "Backup key has an incorrect length.");
			}
			if (new_backup_key_length != BACKUP_KEY_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "New backup key has an incorrect length.");
			}

			if (!users) {
				if (sodium_init() == -1) {
					throw MolchException(INIT_ERROR, "Failed to init libsodium.");
				}
			}

			//unpack the encrypted backup
			auto encrypted_backup_struct = std::unique_ptr<EncryptedBackup,EncryptedBackupDeleter>(encrypted_backup__unpack(&protobuf_c_allocators, backup_length, backup));
			if (encrypted_backup_struct == nullptr) {
				throw MolchException(PROTOBUF_UNPACK_ERROR, "Failed to unpack encrypted backup from protobuf.");
			}

			//check the backup
			if (encrypted_backup_struct->backup_version != 0) {
				throw MolchException(INCORRECT_DATA, "Incompatible backup.");
			}
			if (!encrypted_backup_struct->has_backup_type || (encrypted_backup_struct->backup_type != ENCRYPTED_BACKUP__BACKUP_TYPE__FULL_BACKUP)) {
				throw MolchException(INCORRECT_DATA, "Backup is not a full backup.");
			}
			if (!encrypted_backup_struct->has_encrypted_backup || (encrypted_backup_struct->encrypted_backup.len < crypto_secretbox_MACBYTES)) {
				throw MolchException(PROTOBUF_MISSING_ERROR, "The backup is missing the encrypted state.");
			}
			if (!encrypted_backup_struct->has_encrypted_backup_nonce || (encrypted_backup_struct->encrypted_backup_nonce.len != BACKUP_NONCE_SIZE)) {
				throw MolchException(PROTOBUF_MISSING_ERROR, "The backup is missing the nonce.");
			}

			Buffer decrypted_backup(
					encrypted_backup_struct->encrypted_backup.len - crypto_secretbox_MACBYTES,
					encrypted_backup_struct->encrypted_backup.len - crypto_secretbox_MACBYTES,
					zeroed_malloc, zeroed_free);

			//decrypt the backup
			int status_int = crypto_secretbox_open_easy(
					decrypted_backup.content,
					encrypted_backup_struct->encrypted_backup.data,
					encrypted_backup_struct->encrypted_backup.len,
					encrypted_backup_struct->encrypted_backup_nonce.data,
					backup_key);
			if (status_int != 0) {
				throw MolchException(DECRYPT_ERROR, "Failed to decrypt backup.");
			}

			//unpack the struct
			auto backup_struct = std::unique_ptr<Backup,BackupDeleter>(backup__unpack(&protobuf_c_allocators, decrypted_backup.size, decrypted_backup.content));
			if (backup_struct == nullptr) {
				throw MolchException(PROTOBUF_UNPACK_ERROR, "Failed to unpack backups protobuf-c.");
			}

			//import the user store
			auto store = std::make_unique<UserStore>(backup_struct->users, backup_struct->n_users);

			//update the backup key
			return_status status = molch_update_backup_key(new_backup_key, new_backup_key_length);
			on_error {
				throw MolchException(status);
			}

			//everyting worked, switch to the new user store
			users.reset(store.release());
		} catch (const MolchException& exception) {
			status = exception.toReturnStatus();
			goto cleanup;
		} catch (const std::exception& exception) {
			THROW(EXCEPTION, exception.what());
		}

	cleanup:
		return status;
	}

	/*
	 * Get a signed list of prekeys for a given user.
	 *
	 * Don't forget to destroy the return status with molch_destroy_return_status()
	 * if an error has occured.
	 */
	return_status molch_get_prekey_list(
			//output
			unsigned char ** const prekey_list,  //free after use
			size_t * const prekey_list_length,
			//input
			unsigned char * const public_master_key,
			const size_t public_master_key_length) {
		return_status status = return_status_init();

		try {
			if (!users) {
				throw MolchException(INIT_ERROR, "Molch hasn't been initialized yet.");
			}

			// check input
			if ((public_master_key == nullptr) || (prekey_list == nullptr) || (prekey_list_length == nullptr)) {
				throw MolchException(INVALID_INPUT, "Invalid input to molch_get_prekey_list.");
			}

			if (public_master_key_length != PUBLIC_MASTER_KEY_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "Public master key has an incorrect length.");
			}

			Buffer public_signing_key_buffer(public_master_key, PUBLIC_MASTER_KEY_SIZE);

			auto prekey_list_buffer = create_prekey_list(public_signing_key_buffer);
			Buffer malloced_prekey_list(prekey_list_buffer.size, 0, &malloc, &free);
			malloced_prekey_list.cloneFrom(prekey_list_buffer);
			*prekey_list_length = malloced_prekey_list.size;
			*prekey_list = malloced_prekey_list.release();
		} catch (const MolchException& exception) {
			status = exception.toReturnStatus();
			goto cleanup;
		} catch (const std::exception& exception) {
			THROW(EXCEPTION, exception.what());
		}

	cleanup:
		return status;
	}

	/*
	 * Generate and return a new key for encrypting the exported library state.
	 *
	 * Don't forget to destroy the return status with molch_destroy_return_status()
	 * if an error has occured.
	 */
	return_status molch_update_backup_key(
			unsigned char * const new_key, //output, BACKUP_KEY_SIZE
			const size_t new_key_length) {
		return_status status = return_status_init();

		try {
			if (!users) {
				if (sodium_init() == -1) {
					throw MolchException(INIT_ERROR, "Failed to initialize libsodium.");
				}
				users = std::make_unique<UserStore>();
			}

			if (new_key == nullptr) {
				throw MolchException(INVALID_INPUT, "Invalid input to molch_update_backup_key.");
			}

			if (new_key_length != BACKUP_KEY_SIZE) {
				throw MolchException(INCORRECT_BUFFER_SIZE, "New key has an incorrect length.");
			}

			// create a backup key buffer if it doesnt exist already
			if (global_backup_key == nullptr) {
				global_backup_key = std::unique_ptr<Buffer,GlobalBackupDeleter>(new Buffer(BACKUP_KEY_SIZE, 0, &sodium_malloc, &sodium_free));
			}

			//make the content of the backup key writable
			GlobalBackupKeyWriteUnlocker unlocker;

			global_backup_key->fillRandom(BACKUP_KEY_SIZE);

			Buffer new_key_buffer(new_key, BACKUP_KEY_SIZE);
			new_key_buffer.cloneFrom(*global_backup_key);
		} catch (const MolchException& exception) {
			status = exception.toReturnStatus();
			goto cleanup;
		} catch (const std::exception& exception) {
			THROW(EXCEPTION, exception.what());
		}

	cleanup:
		return status;
}

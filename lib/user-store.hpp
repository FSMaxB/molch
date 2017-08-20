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

#ifndef LIB_USER_STORE_H
#define LIB_USER_STORE_H

#include <sodium.h>
#include <ctime>
#include <memory>
#include <ostream>

#include "constants.h"
#include "buffer.hpp"
#include "conversation-store.hpp"
#include "prekey-store.hpp"
#include "master-keys.hpp"
#include "protobuf-deleters.hpp"

//The user store stores a list of all users identified by their public keys

namespace Molch {
	class UserStoreNode {
		friend class UserStore;
	private:
		unsigned char public_signing_key_storage[PUBLIC_MASTER_KEY_SIZE];

		void exportPublicKeys(
				Buffer * const public_signing_key, //output, optional, can be nullptr
				Buffer * const public_identity_key); //output, optional, can be nullptr

		/*! Import a user from a Protobuf-C struct
		 * \param user The struct to import from.
		 */
		UserStoreNode(const User& user);

		std::unique_ptr<User,UserDeleter> exportProtobuf();

		UserStoreNode& move(UserStoreNode&& node);

	public:
		Buffer public_signing_key{this->public_signing_key_storage, sizeof(this->public_signing_key_storage), 0};
		MasterKeys master_keys;
		PrekeyStore prekeys;
		ConversationStore conversations;

		/*
		 * Create a new user.
		 *
		 * The seed is optional an can be used to add entropy in addition
		 * to the entropy provided by the OS. IMPORTANT: Don't put entropy in
		 * here, that was generated by the OSs CPRNG!
		 */
		UserStoreNode(
				const Buffer& seed,
				Buffer * const public_signing_key = nullptr, //output, optional, can be nullptr
				Buffer * const public_identity_key = nullptr); //output, optional, can be nullptr
		UserStoreNode(
				Buffer * const public_signing_key = nullptr, //output, optional, can be nullptr
				Buffer * const public_identity_key = nullptr); //output, optional, can be nullptr

		UserStoreNode(const UserStoreNode& node) = delete;
		UserStoreNode(UserStoreNode&& node);

		UserStoreNode& operator=(const UserStoreNode& node) = delete;
		UserStoreNode& operator=(UserStoreNode&& node);

		std::ostream& print(std::ostream& stream) const;
	};

	//header of the user store
	class UserStore {
	private:
			std::vector<UserStoreNode> users;

	public:
		UserStore() = default;

		/*! Import a user store from an array of Protobuf-C structs
		 * \param users The array to import from.
		 * \param users_length The length of the array.
		 */
		UserStore(User** const& users, const size_t users_length);

		UserStore(const UserStore& store) = delete;
		UserStore(UserStore&& store) = default;

		UserStore& operator=(const UserStore& store) = delete;
		UserStore& operator=(UserStore&& store) = default;

		void add(UserStoreNode&& user);

		/*
		 * Find a user with a given public signing key.
		 *
		 * Returns nullptr if no user was found.
		 */
		UserStoreNode* find(const Buffer& public_signing_key);

		/*
		 * Find a conversation with a given public signing key.
		 *
		 * return nullptr if no conversation was found.
		 */
		ConversationT* findConversation(UserStoreNode*& user, const Buffer& conversation_id);

		/*
		 * List all of the users.
		 *
		 * Returns a buffer containing a list of all the public
		 * signing keys of the user.
		 */
		Buffer list();

		void remove(const Buffer& public_signing_key);
		void remove(const UserStoreNode* const user);

		void clear();

		/*! Export a user store to an array of Protobuf-C structs
		 * \param store The user store to export
		 * \param users The array to export to.
		 * \param users_length The length of the exported array.
		 * \return The status.
		 */
		void exportProtobuf(User**& users, size_t& users_length);

		size_t size() const;

		std::ostream& print(std::ostream& stream) const;
	};
}

#endif

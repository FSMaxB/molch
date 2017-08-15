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

#include "constants.h"
#include "buffer.hpp"
#include "conversation-store.hpp"
#include "prekey-store.hpp"
#include "master-keys.hpp"

//The user store stores a linked list of all users identified by their private keys
//This linked list is supposed to be stored once in a global variable.

//node of the linked list
typedef struct user_store_node user_store_node;
struct user_store_node {
	user_store_node *previous;
	user_store_node *next;
	Buffer public_signing_key[1];
	unsigned char public_signing_key_storage[PUBLIC_MASTER_KEY_SIZE];
	MasterKeys *master_keys;
	PrekeyStore *prekeys;
	ConversationStore* conversations;
};

//header of the user store
typedef struct user_store {
	size_t length;
	user_store_node *head;
	user_store_node *tail;
} user_store;

//create a new user store
return_status user_store_create(user_store ** const store) noexcept __attribute__((warn_unused_result));

//destroy a user store
void user_store_destroy(user_store * const store) noexcept;

/*
 * Create a new user and add it to the user store.
 *
 * The seed is optional an can be used to add entropy in addition
 * to the entropy provided by the OS. IMPORTANT: Don't put entropy in
 * here, that was generated by the OSs CPRNG!
 */
return_status user_store_create_user(
		user_store * const keystore,
		Buffer * const seed, //optional, can be nullptr
		Buffer * const public_signing_key, //output, optional, can be nullptr
		Buffer * const public_identity_key //output, optional, can be nullptr
		) noexcept __attribute__((warn_unused_result));

/*
 * Find a user for a given public signing key.
 *
 * Returns nullptr if no user was found.
 */
return_status user_store_find_node(user_store_node ** const node, user_store * const store, Buffer * const public_signing_key) noexcept __attribute__((warn_unused_result));

/*
 * List all of the users.
 *
 * Returns a buffer containing a list of all the public
 * signing keys of the user.
 *
 * The buffer is heap allocated, so don't forget to free it!
 */
return_status user_store_list(Buffer ** const list, user_store * const store) noexcept __attribute__((warn_unused_result));

/*
 * Remove a user from the user store.
 *
 * The user is identified by it's public signing key.
 */
return_status user_store_remove_by_key(user_store * const store, Buffer * const public_signing_key) noexcept;

//remove a user from the user store
void user_store_remove(user_store * const store, user_store_node *node) noexcept;

//clear the entire user store
void user_store_clear(user_store *keystore) noexcept;

/*! Export a user store to an array of Protobuf-C structs
 * \param store The user store to export
 * \param users The array to export to.
 * \param users_length The length of the exported array.
 * \return The status.
 */
return_status user_store_export(
	const user_store * const store,
	User *** const users,
	size_t * const users_length) noexcept __attribute__((warn_unused_result));

/*! Import a user store from an array of Protobuf-C structs
 * \param store The user store to import.
 * \param users The array to import from.
 * \param users_length The length of the array.
 * \return The status.
 */
return_status user_store_import(
	user_store ** const store,
	User ** users,
	const size_t users_length) noexcept __attribute__((warn_unused_result));

#endif
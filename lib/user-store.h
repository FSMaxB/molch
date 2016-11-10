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
#include <time.h>

#include "constants.h"
#include "../buffer/buffer.h"
#include "../mcJSON/mcJSON.h"
#include "conversation-store.h"
#include "prekey-store.h"
#include "master-keys.h"
#include "common.h"

#ifndef LIB_USER_STORE_H
#define LIB_USER_STORE_H

//The user store stores a linked list of all users identified by their private keys
//This linked list is supposed to be stored once in a global variable.

//node of the linked list
typedef struct user_store_node user_store_node;
struct user_store_node {
	user_store_node *previous;
	user_store_node *next;
	buffer_t public_signing_key[1];
	unsigned char public_signing_key_storage[PUBLIC_MASTER_KEY_SIZE];
	master_keys *master_keys;
	prekey_store *prekeys;
	conversation_store conversations[1];
};

//header of the user store
typedef struct user_store {
	size_t length;
	user_store_node *head;
	user_store_node *tail;
} user_store;

//create a new user store
return_status user_store_create(user_store ** const store) __attribute__((warn_unused_result));

//destroy a user store
void user_store_destroy(user_store * const store);

/*
 * Create a new user and add it to the user store.
 *
 * The seed is optional an can be used to add entropy in addition
 * to the entropy provided by the OS. IMPORTANT: Don't put entropy in
 * here, that was generated by the OSs CPRNG!
 */
return_status user_store_create_user(
		user_store * const keystore,
		const buffer_t * const seed, //optional, can be NULL
		buffer_t * const public_signing_key, //output, optional, can be NULL
		buffer_t * const public_identity_key //output, optional, can be NULL
		) __attribute__((warn_unused_result));

/*
 * Find a user for a given public signing key.
 *
 * Returns NULL if no user was found.
 */
return_status user_store_find_node(user_store_node ** const node, user_store * const store, const buffer_t * const public_signing_key) __attribute__((warn_unused_result));

/*
 * List all of the users.
 *
 * Returns a buffer containing a list of all the public
 * signing keys of the user.
 *
 * The buffer is heap allocated, so don't forget to free it!
 */
return_status user_store_list(buffer_t ** const list, user_store * const store) __attribute__((warn_unused_result));

/*
 * Remove a user from the user store.
 *
 * The user is identified by it's public signing key.
 */
return_status user_store_remove_by_key(user_store * const store, const buffer_t * const public_signing_key);

//remove a user from the user store
void user_store_remove(user_store * const store, user_store_node *node);

//clear the entire user store
void user_store_clear(user_store *keystore);

/*
 * Serialise a user store into JSON. It get's a mempool_t buffer and stores a tree of
 * mcJSON objects into the buffer starting at pool->position.
 *
 * Returns NULL in case of Failure.
 */
mcJSON *user_store_json_export(user_store * const store, mempool_t * const pool) __attribute__((warn_unused_result));

/*
 * Deserialise a user store (import from JSON).
 */
user_store *user_store_json_import(const mcJSON * const json) __attribute__((warn_unused_result));
#endif

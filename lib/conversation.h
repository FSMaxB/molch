/* Molch, an implementation of the axolotl ratchet based on libsodium
 *  Copyright (C) 2015  Max Bruckner (FSMaxB)
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "constants.h"
#include "ratchet.h"
#include "prekey-store.h"

#ifndef LIB_CONVERSATION_H
#define LIB_CONVERSATION_H

typedef struct conversation_t {
	buffer_t id[1]; //unique id of a conversation, generated randomly
	unsigned char id_storage[CONVERSATION_ID_SIZE];
	ratchet_state *ratchet;
} conversation_t;

/*
 * Create a new conversation
 */
int conversation_init(
		conversation_t * const conversation,
		const buffer_t * const our_private_identity,
		const buffer_t * const our_public_identity,
		const buffer_t * const their_public_identity,
		const buffer_t * const our_private_ephemeral,
		const buffer_t * const our_public_ephemeral,
		const buffer_t * const their_public_ephemeral) __attribute__((warn_unused_result));

/*
 * Destroy a conversation.
 */
void conversation_deinit(conversation_t * const conversation);

/*
 * Serialise a conversation into JSON. It get#s a mempool_t buffer and stores a tree of
 * mcJSON objects into the buffer starting at pool->position.
 *
 * Returns NULL in case of failure.
 */
mcJSON *conversation_json_export(const conversation_t * const conversation, mempool_t * const pool) __attribute__((warn_unused_result));

/*
 * Deserialize a conversation (import from JSON)
 */
int conversation_json_import(
		const mcJSON * const json,
		conversation_t * const conversation) __attribute__((warn_unused_result));

/*
 * Start a new conversation where we are the sender.
 */
int conversation_start_send_conversation(
		conversation_t *const conversation, //conversation to initialize
		const buffer_t *const message, //message we want to send to the receiver
		buffer_t ** packet, //output, free after use!
		const buffer_t * const sender_public_identity, //who is sending this message?
		const buffer_t * const sender_private_identity,
		const buffer_t * const receiver_public_identity,
		const buffer_t * const receiver_prekey_list //PREKEY_AMOUNT * PUBLIC_KEY_SIZE
		) __attribute__((warn_unused_result));

/*
 * Start a new conversation where we are the receiver.
 */
int conversation_start_receive_conversation(
		conversation_t * const conversation, //conversation to initialize
		const buffer_t * const packet, //received packet
		buffer_t ** message, //output, free after use!
		const buffer_t * const receiver_public_identity,
		const buffer_t * const receiver_private_identity,
		prekey_store * const receiver_prekeys //prekeys of the receiver
		) __attribute__((warn_unused_result));

/*
 * Send a message using an existing conversation.
 */
int conversation_send(
		conversation_t * const conversation,
		const buffer_t * const message,
		buffer_t **packet, //output, free after use!
		const buffer_t * const public_identity_key, //can be NULL, if not NULL, this will be a prekey message
		const buffer_t * const public_ephemeral_key, //cann be NULL, if not NULL, this will be a prekey message
		const buffer_t * const public_prekey //can be NULL, if not NULL, this will be a prekey message
		) __attribute__((warn_unused_result));

/*
 * Receive and decrypt a message using an existing conversation.
 */
int conversation_receive(
	conversation_t * const conversation,
	const buffer_t * const packet, //received packet
	buffer_t ** const message //output, free after use!
		) __attribute__((warn_unused_result));
#endif

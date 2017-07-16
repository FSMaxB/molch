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

#include "../lib/ratchet.h"
#include "utils.h"

static int keypair(Buffer *private_key, Buffer *public_key) noexcept {
	return crypto_box_keypair(public_key->content, private_key->content);
}

int main(void) noexcept {
	int status_int = sodium_init();
	if (status_int != 0) {
		return status_int;
	}

	return_status status = return_status_init();

	//create all the buffers
	//Keys:
	//Alice:
	Buffer *alice_private_identity = Buffer::create(PRIVATE_KEY_SIZE, PRIVATE_KEY_SIZE);
	Buffer *alice_public_identity = Buffer::create(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);
	Buffer *alice_private_ephemeral = Buffer::create(PRIVATE_KEY_SIZE, PRIVATE_KEY_SIZE);
	Buffer *alice_public_ephemeral = Buffer::create(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);
	//Bob
	Buffer *bob_private_identity = Buffer::create(PRIVATE_KEY_SIZE, PRIVATE_KEY_SIZE);
	Buffer *bob_public_identity = Buffer::create(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);
	Buffer *bob_private_ephemeral = Buffer::create(PRIVATE_KEY_SIZE, PRIVATE_KEY_SIZE);
	Buffer *bob_public_ephemeral = Buffer::create(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);

	//keys for sending
	Buffer *send_header_key = Buffer::create(HEADER_KEY_SIZE, HEADER_KEY_SIZE);
	Buffer *send_message_key = Buffer::create(MESSAGE_KEY_SIZE, MESSAGE_KEY_SIZE);
	Buffer *public_send_ephemeral = Buffer::create(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);

	//keys for receiving
	Buffer *current_receive_header_key = Buffer::create(HEADER_KEY_SIZE, HEADER_KEY_SIZE);
	Buffer *next_receive_header_key = Buffer::create(HEADER_KEY_SIZE, HEADER_KEY_SIZE);
	Buffer *receive_message_key = Buffer::create(MESSAGE_KEY_SIZE, MESSAGE_KEY_SIZE);

	//ratchets
	Ratchet *alice_send_ratchet = nullptr;
	Ratchet *alice_receive_ratchet = nullptr;
	Ratchet *bob_send_ratchet = nullptr;
	Ratchet *bob_receive_ratchet = nullptr;

	//generate the keys
	if (keypair(alice_private_identity, alice_public_identity) != 0) {
		THROW(KEYGENERATION_FAILED, "Failed to generate Alice' identity keypair.");
	}
	if (keypair(alice_private_ephemeral, alice_public_ephemeral) != 0) {
		THROW(KEYGENERATION_FAILED, "Failed to generate Alice' ephemeral keypair.");
	}
	if (keypair(bob_private_identity, bob_public_identity) != 0) {
		THROW(KEYGENERATION_FAILED, "Failed to generate Bobs identity keypair.");
	}
	if (keypair(bob_private_ephemeral, bob_public_ephemeral) != 0) {
		THROW(KEYGENERATION_FAILED, "Failed to generate Bobs ephemeral keypair.");
	}

	//compare public identity keys, the one with the bigger key will be alice
	//(to make the test more predictable, and make the 'am_i_alice' flag in the
	// ratchet match the names here)
	if (sodium_compare(bob_public_identity->content, alice_public_identity->content, PUBLIC_KEY_SIZE) > 0) {
		//swap bob and alice
		//public identity key
		Buffer *stash = alice_public_identity;
		alice_public_identity = bob_public_identity;
		bob_public_identity = stash;

		//private identity key
		stash = alice_private_identity;
		alice_private_identity = bob_private_identity;
		bob_private_identity = stash;
	}

	//initialise the ratchets
	//Alice
	status = Ratchet::create(
			alice_send_ratchet,
			*alice_private_identity,
			*alice_public_identity,
			*bob_public_identity,
			*alice_private_ephemeral,
			*alice_public_ephemeral,
			*bob_public_ephemeral);
	THROW_on_error(CREATION_ERROR, "Failed to create Alice' send ratchet.");
	status = Ratchet::create(
			alice_receive_ratchet,
			*alice_private_identity,
			*alice_public_identity,
			*bob_public_identity,
			*alice_private_ephemeral,
			*alice_public_ephemeral,
			*bob_public_ephemeral);
	THROW_on_error(CREATION_ERROR, "Failed to create Alice' receive ratchet.");
	//Bob
	status = Ratchet::create(
			bob_send_ratchet,
			*bob_private_identity,
			*bob_public_identity,
			*alice_public_identity,
			*bob_private_ephemeral,
			*bob_public_ephemeral,
			*alice_public_ephemeral);
	THROW_on_error(CREATION_ERROR, "Failed to create Bobs send ratchet.");
	status = Ratchet::create(
			bob_receive_ratchet,
			*bob_private_identity,
			*bob_public_identity,
			*alice_public_identity,
			*bob_private_ephemeral,
			*bob_public_ephemeral,
			*alice_public_ephemeral);
	THROW_on_error(CREATION_ERROR, "Failed to create Bobs receive ratchet.");

	// FIRST SCENARIO: ALICE SENDS A MESSAGE TO BOB
	uint32_t send_message_number;
	uint32_t previous_send_message_number;
	status = alice_send_ratchet->send(
			*send_header_key,
			send_message_number,
			previous_send_message_number,
			*public_send_ephemeral,
			*send_message_key);
	THROW_on_error(DATA_FETCH_ERROR, "Failed to get send keys.");

	//bob receives
	status = bob_receive_ratchet->getReceiveHeaderKeys(*current_receive_header_key, *next_receive_header_key);
	THROW_on_error(DATA_FETCH_ERROR, "Failed to get receive header keys.");

	ratchet_header_decryptability decryptability;
	if (send_header_key->compare(current_receive_header_key) == 0) {
		decryptability = CURRENT_DECRYPTABLE;
	} else if (send_header_key->compare(next_receive_header_key) == 0) {
		decryptability = NEXT_DECRYPTABLE;
	} else {
		decryptability = UNDECRYPTABLE;
	}
	status = bob_receive_ratchet->setHeaderDecryptability(decryptability);
	THROW_on_error(DATA_SET_ERROR, "Failed to set header decryptability.");

	status = bob_receive_ratchet->receive(
			*receive_message_key,
			*public_send_ephemeral,
			send_message_number,
			previous_send_message_number);
	THROW_on_error(DATA_FETCH_ERROR, "Failed to get receive message key.");

	//now check if the message key is the same
	if (send_message_key->compare(receive_message_key) != 0) {
		THROW(INCORRECT_DATA, "Bobs receive message key isn't the same as Alice' send message key.");
	}
	printf("SUCCESS: Bobs receive message key is the same as Alice' send message key.\n");

	status = bob_receive_ratchet->setLastMessageAuthenticity(true);
	THROW_on_error(DATA_SET_ERROR, "Bob-Receive: Failed to set message authenticity.");


	//SECOND SCENARIO: BOB SENDS MESSAGE TO ALICE
	status = bob_send_ratchet->send(
			*send_header_key,
			send_message_number,
			previous_send_message_number,
			*public_send_ephemeral,
			*send_message_key);
	THROW_on_error(DATA_FETCH_ERROR, "Bob-Send: Failed to get send keys.");

	//alice receives
	status = alice_receive_ratchet->getReceiveHeaderKeys(*current_receive_header_key, *next_receive_header_key);
	THROW_on_error(DATA_FETCH_ERROR, "Alice-Receive: Failed to get receive header keys.");

	if (send_header_key->compare(current_receive_header_key) == 0) {
		decryptability = CURRENT_DECRYPTABLE;
	} else if (send_header_key->compare(next_receive_header_key) == 0) {
		decryptability = UNDECRYPTABLE;
	}
	status = alice_receive_ratchet->setHeaderDecryptability(decryptability);
	THROW_on_error(DATA_SET_ERROR, "Alice-Receive: Failed to set header decryptability.");

	status = alice_receive_ratchet->receive(
			*receive_message_key,
			*public_send_ephemeral,
			send_message_number,
			previous_send_message_number);
	THROW_on_error(RECEIVE_ERROR, "Alice-Receive: Failed to get receive message key.");

	//now check if the message key is the same
	if (send_message_key->compare(receive_message_key) != 0) {
		THROW(INCORRECT_DATA, "Alice' receive message key isn't the same as Bobs send message key.");
	}
	printf("SUCCESS: Alice' receive message key is the same as Bobs send message key.\n");

	status = alice_receive_ratchet->setLastMessageAuthenticity(true);
	THROW_on_error(DATA_SET_ERROR, "Alice-Receive: Failed to set message authenticity.");

	//THIRD SCENARIO: BOB ANSWERS ALICE AFTER HAVING RECEIVED HER FIRST MESSAGE
	status = bob_receive_ratchet->send(
			*send_header_key,
			send_message_number,
			previous_send_message_number,
			*public_send_ephemeral,
			*send_message_key);
	THROW_on_error(DATA_FETCH_ERROR, "Bob-Response: Failed to get send keys.");

	//alice receives
	status = alice_send_ratchet->getReceiveHeaderKeys(*current_receive_header_key, *next_receive_header_key);
	THROW_on_error(DATA_FETCH_ERROR, "Alice-Roundtrip: Failed to get receive header keys.");

	if (send_header_key->compare(current_receive_header_key) == 0) {
		decryptability = CURRENT_DECRYPTABLE;
	} else if (send_header_key->compare(next_receive_header_key) == 0) {
		decryptability = NEXT_DECRYPTABLE;
	} else {
		decryptability = UNDECRYPTABLE;
	}
	status = alice_send_ratchet->setHeaderDecryptability(decryptability);
	THROW_on_error(DATA_SET_ERROR, "Alice-Roundtrip: Failed  to set header decryptability.");

	status = alice_send_ratchet->receive(
			*receive_message_key,
			*public_send_ephemeral,
			send_message_number,
			previous_send_message_number);
	THROW_on_error(RECEIVE_ERROR, "Alice-Roundtrip: Failed to get receive message key.");

	//now check if the message key is the same
	if (send_message_key->compare(receive_message_key) != 0) {
		THROW(INCORRECT_DATA, "Alice' receive message key isn't the same as Bobs send message key.");
	}
	printf("SUCCESS: Alice' receive message key is the same as Bobs send message key.\n");

	status = alice_send_ratchet->setLastMessageAuthenticity(true);
	THROW_on_error(DATA_SET_ERROR, "Alice-Roundtrip: Failed to set message authenticity.");

	//FOURTH SCENARIO: ALICE ANSWERS BOB AFTER HAVING RECEIVED HER FIRST MESSAGE
	status = alice_receive_ratchet->send(
			*send_header_key,
			send_message_number,
			previous_send_message_number,
			*public_send_ephemeral,
			*send_message_key);
	THROW_on_error(DATA_FETCH_ERROR, "Bob-Roundtrip: Failed to get send-keys.");

	//bob receives
	status = bob_send_ratchet->getReceiveHeaderKeys(*current_receive_header_key, *next_receive_header_key);
	THROW_on_error(DATA_FETCH_ERROR, "Bob-Roundtrip: Failed to get receive header keys.");

	if (send_header_key->compare(current_receive_header_key) == 0) {
		decryptability = CURRENT_DECRYPTABLE;
	} else if (send_header_key->compare(next_receive_header_key) == 0) {
		decryptability = NEXT_DECRYPTABLE;
	} else {
		decryptability = UNDECRYPTABLE;
	}
	status = bob_send_ratchet->setHeaderDecryptability(decryptability);
	THROW_on_error(DATA_SET_ERROR, "Bob-Roundtrip: Failed to set header decryptability.");

	status = bob_send_ratchet->receive(
			*receive_message_key,
			*public_send_ephemeral,
			send_message_number,
			previous_send_message_number);
	THROW_on_error(RECEIVE_ERROR, "Bob-Roundtrip: Failed to get receive message key.");

	//now check if the message key is the same
	if (send_message_key->compare(receive_message_key) != 0) {
		THROW(INCORRECT_DATA, "Bobs receive message key isn't the same as Alice' send message key.");
	}
	printf("SUCCESS: Bobs receive message key is the same as Alice' send message key.\n");

	status = bob_send_ratchet->setLastMessageAuthenticity(true);
	THROW_on_error(DATA_SET_ERROR, "Bob-Roundtrip: Failed to set message authenticity.");

cleanup:
	buffer_destroy_from_heap_and_null_if_valid(alice_private_identity);
	buffer_destroy_from_heap_and_null_if_valid(alice_public_identity);
	buffer_destroy_from_heap_and_null_if_valid(alice_private_ephemeral);
	buffer_destroy_from_heap_and_null_if_valid(alice_public_ephemeral);
	buffer_destroy_from_heap_and_null_if_valid(bob_private_identity);
	buffer_destroy_from_heap_and_null_if_valid(bob_public_identity);
	buffer_destroy_from_heap_and_null_if_valid(bob_private_ephemeral);
	buffer_destroy_from_heap_and_null_if_valid(bob_public_ephemeral);

	buffer_destroy_from_heap_and_null_if_valid(send_header_key);
	buffer_destroy_from_heap_and_null_if_valid(send_message_key);
	buffer_destroy_from_heap_and_null_if_valid(public_send_ephemeral);
	buffer_destroy_from_heap_and_null_if_valid(current_receive_header_key);
	buffer_destroy_from_heap_and_null_if_valid(next_receive_header_key);
	buffer_destroy_from_heap_and_null_if_valid(receive_message_key);

	if (alice_send_ratchet != nullptr) {
		alice_send_ratchet->destroy();
	}
	if (alice_receive_ratchet != nullptr) {
		alice_receive_ratchet->destroy();
	}
	if (bob_send_ratchet != nullptr) {
		bob_send_ratchet->destroy();
	}
	if (bob_receive_ratchet != nullptr) {
		bob_receive_ratchet->destroy();
	}

	on_error {
		print_errors(&status);
	}
	return_status_destroy_errors(&status);

	return status.status;
}

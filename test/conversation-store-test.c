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

#include <stdio.h>
#include <stdlib.h>
#include <sodium.h>
#include <assert.h>
#include <alloca.h>

#include "../lib/conversation-store.h"
#include "../lib/json.h"
#include "utils.h"
#include "tracing.h"

return_status protobuf_export(
		const conversation_store * const store,
		buffer_t *** const export_buffers,
		size_t * const buffer_count) __attribute__((warn_unused_result));
return_status protobuf_export(
		const conversation_store * const store,
		buffer_t *** const export_buffers,
		size_t * const buffer_count) {
	return_status status = return_status_init();

	Conversation ** conversations = NULL;
	size_t length = 0;

	if (export_buffers != NULL) {
		*export_buffers = NULL;
	}
	if (buffer_count != NULL) {
		*buffer_count = 0;
	}

	//check input
	if ((store == NULL) || (export_buffers == NULL) || (buffer_count == NULL)) {
		throw(INVALID_INPUT, "Invalid input to protobuf_export.");
	}

	status = conversation_store_export(store, &conversations, &length);
	throw_on_error(EXPORT_ERROR, "Failed to export conversations.");

	*export_buffers = malloc(length * sizeof(buffer_t*));
	throw_on_failed_alloc(*export_buffers);
	*buffer_count = length;

	//unpack all the conversations
	for (size_t i = 0; i < length; i++) {
		size_t unpacked_size = conversation__get_packed_size(conversations[i]);
		(*export_buffers)[i] = buffer_create_on_heap(unpacked_size, 0);
		throw_on_failed_alloc((*export_buffers)[i]);

		(*export_buffers)[i]->content_length = conversation__pack(conversations[i], (*export_buffers)[i]->content);
	}

cleanup:
	if (conversations != NULL) {
		for (size_t i = 0; i < length; i++) {
			if (conversations[i] != NULL) {
				conversation__free_unpacked(conversations[i], &protobuf_c_allocators);
				conversations[i] = NULL;
			}
		}
		zeroed_free_and_null_if_valid(conversations);
	}
	//buffer will be freed in main
	return status;
}

return_status test_add_conversation(conversation_store * const store) {
	//define key buffers
	//identity keys
	buffer_t *our_private_identity = buffer_create_on_heap(crypto_box_SECRETKEYBYTES, crypto_box_SECRETKEYBYTES);
	buffer_t *our_public_identity = buffer_create_on_heap(crypto_box_PUBLICKEYBYTES, crypto_box_PUBLICKEYBYTES);
	buffer_t *their_public_identity = buffer_create_on_heap(crypto_box_PUBLICKEYBYTES, crypto_box_PUBLICKEYBYTES);
	//ephemeral keys
	buffer_t *our_private_ephemeral = buffer_create_on_heap(crypto_box_SECRETKEYBYTES, crypto_box_SECRETKEYBYTES);
	buffer_t *our_public_ephemeral= buffer_create_on_heap(crypto_box_PUBLICKEYBYTES, crypto_box_PUBLICKEYBYTES);
	buffer_t *their_public_ephemeral = buffer_create_on_heap(crypto_box_PUBLICKEYBYTES, crypto_box_PUBLICKEYBYTES);

	conversation_t *conversation = NULL;

	return_status status = return_status_init();

	//generate the keys
	int status_int = 0;

	status_int = crypto_box_keypair(our_public_identity->content, our_private_identity->content);
	if (status_int != 0) {
		throw(KEYGENERATION_FAILED, "Failed to generate our identity keys.");
	}
	status_int = crypto_box_keypair(our_public_ephemeral->content, our_private_ephemeral->content);
	if (status_int != 0) {
		throw(KEYGENERATION_FAILED, "Failed to generate our ephemeral keys.");
	}
	status_int = buffer_fill_random(their_public_identity, their_public_identity->buffer_length);
	if (status_int != 0) {
		throw(KEYGENERATION_FAILED, "Failed to generate their public identity keys.");
	}
	status_int = buffer_fill_random(their_public_ephemeral, their_public_ephemeral->buffer_length);
	if (status_int != 0) {
		throw(KEYGENERATION_FAILED, "Failed to generate their public ephemeral keys.");
	}

	//create the conversation manually
	conversation = malloc(sizeof(conversation_t));
	if (conversation == NULL) {
		throw(ALLOCATION_FAILED, "Failed to allocate conversation.");
	}

	conversation->next = NULL;
	conversation->previous = NULL;
	conversation->ratchet = NULL;

	//create the conversation id
	buffer_init_with_pointer(conversation->id, conversation->id_storage, CONVERSATION_ID_SIZE, CONVERSATION_ID_SIZE);

	status_int = buffer_fill_random(conversation->id, CONVERSATION_ID_SIZE);
	if (status_int != 0) {
		throw(GENERIC_ERROR, "Failed to fill buffer with random data.");
	}

	status = ratchet_create(
			&(conversation->ratchet),
			our_private_identity,
			our_public_identity,
			their_public_identity,
			our_private_ephemeral,
			our_public_ephemeral,
			their_public_ephemeral);
	if (conversation->ratchet == NULL) {
		throw(CREATION_ERROR, "Failed to creat ratchet.");
	}

	status = conversation_store_add(store, conversation);
	throw_on_error(ADDITION_ERROR, "Failed to add conversation to store.");
	conversation = NULL;

	goto cleanup;

cleanup:
	if (conversation != NULL) {
		conversation_destroy(conversation);
	}
	//destroy all the buffers
	buffer_destroy_from_heap_and_null_if_valid(our_private_identity);
	buffer_destroy_from_heap_and_null_if_valid(our_public_identity);
	buffer_destroy_from_heap_and_null_if_valid(their_public_identity);
	buffer_destroy_from_heap_and_null_if_valid(our_private_ephemeral);
	buffer_destroy_from_heap_and_null_if_valid(our_public_ephemeral);
	buffer_destroy_from_heap_and_null_if_valid(their_public_ephemeral);

	return status;
}

int main(void) {
	if (sodium_init() == -1) {
		return -1;
	}

	return_status status = return_status_init();

	//protobuf buffers
	buffer_t ** protobuf_export_buffers = NULL;
	size_t protobuf_export_buffers_length = 0;

	int status_int = EXIT_SUCCESS;
	conversation_store *store = malloc(sizeof(conversation_store));
	if (store == NULL) {
		throw(ALLOCATION_FAILED, "Failed to allocate conversation store.");
	}

	printf("Initialize the conversation store.\n");
	conversation_store_init(store);

	// list an empty conversation store
	buffer_t *empty_list;
	status = conversation_store_list(&empty_list, store);
	throw_on_error(DATA_FETCH_ERROR, "Failed to list empty conversation store.");
	if (empty_list != NULL) {
		throw(INCORRECT_DATA, "List of empty conversation store is not NULL.");
	}

	// add five conversations
	printf("Add five conversations.\n");
	for (size_t i = 0; i < 5; i++) {
		printf("%zu\n", i);
		status = test_add_conversation(store);
		throw_on_error(ADDITION_ERROR, "Failed to add test conversation.");
		if (store->length != (i + 1)) {
			throw(INCORRECT_DATA, "Conversation store has incorrect length.");
		}
	}

	//show all the conversation ids
	printf("Conversation IDs (test of foreach):\n");
	conversation_store_foreach(store,
		printf("ID of the conversation No. %zu:\n", index);
		print_hex(value->id);
		putchar('\n');
	)

	//find node by id
	conversation_t *found_node = NULL;
	status = conversation_store_find_node(&found_node, store, store->head->next->next->id);
	throw_on_error(NOT_FOUND, "Failed to find conversation.");
	if (found_node != store->head->next->next) {
		throw(NOT_FOUND, "Failed to find node by ID.");
	}
	printf("Found node by ID.\n");

	//test list export feature
	buffer_t *conversation_list = NULL;
	status = conversation_store_list(&conversation_list, store);
	on_error(
		throw(DATA_FETCH_ERROR, "Failed to list conversations.");
	)
	if ((conversation_list == NULL) || (conversation_list->content_length != (CONVERSATION_ID_SIZE * store->length))) {
		throw(DATA_FETCH_ERROR, "Failed to get list of conversations.");
	}

	//check for all conversations that they exist
	for (size_t i = 0; i < (conversation_list->content_length / CONVERSATION_ID_SIZE); i++) {
		buffer_create_with_existing_array(current_id, conversation_list->content + CONVERSATION_ID_SIZE * i, CONVERSATION_ID_SIZE);
		status = conversation_store_find_node(&found_node, store, current_id);
		if ((status.status != SUCCESS) || (found_node == NULL)) {
			buffer_destroy_from_heap_and_null_if_valid(conversation_list);
			throw(INCORRECT_DATA, "Exported list of conversations was incorrect.");
		}
	}
	buffer_destroy_from_heap_and_null_if_valid(conversation_list);

	//test protobuf export
	printf("Export to Protobuf-C\n");
	status = protobuf_export(store, &protobuf_export_buffers, &protobuf_export_buffers_length);
	throw_on_error(EXPORT_ERROR, "Failed to export conversation store.");

	printf("protobuf_export_buffers_length = %zu\n", protobuf_export_buffers_length);
	//print
	puts("[\n");
	for (size_t i = 0; i < protobuf_export_buffers_length; i++) {
		print_hex(protobuf_export_buffers[i]);
		puts(",\n");
	}
	puts("]\n\n");

	//test JSON export
	printf("Test JSON export!\n");
	mempool_t *pool = buffer_create_on_heap(100000, 0);
	mcJSON *json = conversation_store_json_export(store, pool);
	if (json == NULL) {
		buffer_destroy_from_heap_and_null_if_valid(pool);
		throw(EXPORT_ERROR, "Failed to export JSON.");
	}
	buffer_t *output = mcJSON_PrintBuffered(json, 4000, true);
	if (output == NULL) {
		buffer_destroy_from_heap_and_null_if_valid(pool);
		buffer_destroy_from_heap_and_null_if_valid(output);
		throw(GENERIC_ERROR, "Failed to print json.");
	}
	printf("%.*s\n", (int)output->content_length, output->content);
	if (json->length != 5) {
		buffer_destroy_from_heap_and_null_if_valid(pool);
		buffer_destroy_from_heap_and_null_if_valid(output);
		throw(INCORRECT_DATA, "Exported JSON doesn't contain all conversations.");
	}
	buffer_destroy_from_heap_and_null_if_valid(pool);

	//test JSON import
	conversation_store *imported_store = malloc(sizeof(conversation_store));
	if (imported_store == NULL) {
		buffer_destroy_from_heap_and_null_if_valid(output);
		throw(ALLOCATION_FAILED, "Failed to allocate conversation store.");
	}
	JSON_INITIALIZE(imported_store, 100000, output, conversation_store_json_import, status_int);
	if (status_int != 0) {
		free_and_null_if_valid(imported_store);
		buffer_destroy_from_heap_and_null_if_valid(output);
		throw(IMPORT_ERROR, "Failed to import from JSON.");
	}
	//export the imported to json again
	JSON_EXPORT(imported_output, 100000, 4000, true, imported_store, conversation_store_json_export);
	if (imported_output == NULL) {
		conversation_store_clear(imported_store);
		free_and_null_if_valid(imported_store);
		buffer_destroy_from_heap_and_null_if_valid(output);
		throw(GENERIC_ERROR, "Failed to print imported output.");
	}
	conversation_store_clear(imported_store);
	free_and_null_if_valid(imported_store);
	//compare both JSON strings
	if (buffer_compare(imported_output, output) != 0) {
		buffer_destroy_from_heap_and_null_if_valid(output);
		buffer_destroy_from_heap_and_null_if_valid(imported_output);
		throw(INCORRECT_DATA, "Imported conversation store is incorrect.");
	}
	buffer_destroy_from_heap_and_null_if_valid(output);
	buffer_destroy_from_heap_and_null_if_valid(imported_output);

	//remove nodes
	conversation_store_remove(store, store->head);
	printf("Removed head.\n");
	conversation_store_remove(store, store->tail);
	printf("Removed tail.\n");
	conversation_store_remove(store, store->head->next);

	if (store->length != 2) {
		throw(REMOVE_ERROR, "Failed to remove nodes.");
	}
	printf("Successfully removed nodes.\n");

	//remove node by id
	conversation_store_remove_by_id(store, store->tail->id);
	if (store->length != 1) {
		throw(REMOVE_ERROR, "Failed to remove node by id.");
	}
	printf("Successfully removed node by id.\n");

	//clear the conversation store
	printf("Clear the conversation store.\n");

cleanup:
	if (protobuf_export_buffers != NULL) {
		for (size_t i =0; i < protobuf_export_buffers_length; i++) {
			buffer_destroy_from_heap_and_null_if_valid(protobuf_export_buffers[i]);
		}
		free_and_null_if_valid(protobuf_export_buffers);
	}

	conversation_store_clear(store);
	free_and_null_if_valid(store);

	on_error(
		print_errors(&status);
	)
	return_status_destroy_errors(&status);

	return status.status;
}

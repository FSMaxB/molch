/* Molch, an implementation of the axolotl ratchet based on libsodium
 *  Copyright (C) 2015-2016 1984not Security GmbH
 *  Author: Max Bruckner (FSMaxB)
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
#include <stdio.h>
#include <stdlib.h>
#include <sodium.h>

#include "../lib/packet.h"
#include "../lib/molch.h"
#include "../lib/constants.h"
#include "utils.h"
#include "packet-test-lib.h"
#include "tracing.h"

int main(void) {
	if (sodium_init() == -1) {
		return -1;
	}

	buffer_create_from_string(message, "Hello world!\n");
	//create buffers
	buffer_t *header_key = buffer_create_on_heap(crypto_aead_chacha20poly1305_KEYBYTES, crypto_aead_chacha20poly1305_KEYBYTES);
	buffer_t *message_key = buffer_create_on_heap(crypto_secretbox_KEYBYTES, crypto_secretbox_KEYBYTES);
	buffer_t *public_identity_key = buffer_create_on_heap(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);
	buffer_t *public_ephemeral_key = buffer_create_on_heap(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);
	buffer_t *public_prekey = buffer_create_on_heap(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);
	buffer_t *header = buffer_create_on_heap(4, 4);
	buffer_t *packet = buffer_create_on_heap(3 + crypto_aead_chacha20poly1305_NPUBBYTES + crypto_aead_chacha20poly1305_ABYTES + crypto_secretbox_NONCEBYTES + message->content_length + header->content_length + crypto_secretbox_MACBYTES + 255 + 3 * PUBLIC_KEY_SIZE, 3 + crypto_aead_chacha20poly1305_NPUBBYTES + crypto_aead_chacha20poly1305_ABYTES + crypto_secretbox_NONCEBYTES + message->content_length + header->content_length + crypto_secretbox_MACBYTES + 255 + 3 * PUBLIC_KEY_SIZE);
	buffer_t *decrypted_header = buffer_create_on_heap(255, 255);
	buffer_t *decrypted_message_nonce = buffer_create_on_heap(crypto_secretbox_NONCEBYTES, crypto_secretbox_NONCEBYTES);
	buffer_t *decrypted_message = buffer_create_on_heap(packet->content_length, packet->content_length);

	//generate keys and message
	header->content[0] = 0x01;
	header->content[1] = 0x02;
	header->content[2] = 0x03;
	header->content[3] = 0x04;
	unsigned char packet_type = 1;
	printf("Packet type: %02x\n", packet_type);
	const unsigned char current_protocol_version = 2;
	printf("Current protocol version: %02x\n", current_protocol_version);
	const unsigned char highest_supported_protocol_version = 3;
	printf("Highest supported protocol version: %02x\n", highest_supported_protocol_version);
	putchar('\n');

	//NORMAL MESSAGE
	printf("NORMAL MESSAGE\n");
	int status = create_and_print_message(
			packet,
			packet_type,
			current_protocol_version,
			highest_supported_protocol_version,
			message,
			message_key,
			header,
			header_key,
			NULL,
			NULL,
			NULL);
	if (status != 0) {
		goto cleanup;
	}

	//now decrypt the header
	status = packet_decrypt_header(
			packet,
			decrypted_header,
			decrypted_message_nonce,
			header_key,
			NULL,
			NULL,
			NULL);
	buffer_clear(decrypted_header);
	if (status != 0) {
		fprintf(stderr, "ERROR: Failed to decrypt header. (%i)\n", status);
		goto cleanup;
	}

	printf("Decrypted message nonce (%zu Bytes):\n", decrypted_message_nonce->content_length);
	print_hex(decrypted_message_nonce);
	putchar('\n');

	//now decrypt the message
	status = packet_decrypt_message(
			packet,
			decrypted_message,
			decrypted_message_nonce,
			message_key);
	if (status != 0) {
		fprintf(stderr, "ERROR: Failed to decrypt message. (%i)\n", status);
		goto cleanup;
	}

	//check the message size
	if (decrypted_message->content_length != message->content_length) {
		fprintf(stderr, "ERROR: Decrypted message length isn't the same.\n");
		status = EXIT_FAILURE;
		goto cleanup;
	}
	printf("Decrypted message length is the same.\n");

	//compare the message
	if (buffer_compare(message, decrypted_message) != 0) {
		fprintf(stderr, "ERROR: Decrypted message doesn't match!\n");
		status = EXIT_FAILURE;
		goto cleanup;
	}
	buffer_clear(decrypted_message);
	printf("Decrypted message is the same.\n\n");

	//manipulate the message
	packet->content[packet->content_length - crypto_secretbox_MACBYTES - 1] ^= 0xf0;
	printf("Manipulating message.\n");

	//try to decrypt
	status = packet_decrypt_message(
			packet,
			decrypted_message,
			decrypted_message_nonce,
			message_key);
	if (status == 0) { //message was decrypted although it shouldn't
		fprintf(stderr, "ERROR: Decrypted manipulated message.\n");
		status = EXIT_FAILURE;
		goto cleanup;
	}
	printf("Manipulation detected.\n\n");

	//PREKEY MESSAGE
	printf("PREKEY MESSAGE\n");
	//create the public keys
	status = buffer_fill_random(public_identity_key, PUBLIC_KEY_SIZE);
	if (status != 0) {
		fprintf(stderr, "ERROR: Failed to generate public identity key. (%i)\n", status);
		goto cleanup;
	}
	status = buffer_fill_random(public_ephemeral_key, PUBLIC_KEY_SIZE);
	if (status != 0) {
		fprintf(stderr, "ERROR: Failed to generate public ephemeral key. (%i)\n", status);
		goto cleanup;
	}
	status = buffer_fill_random(public_prekey, PUBLIC_KEY_SIZE);
	if (status != 0) {
		fprintf(stderr, "ERROR: Failed to generate public prekey. (%i)\n", status);
		goto cleanup;
	}

	buffer_clear(packet);
	packet_type = PREKEY_MESSAGE;
	status = create_and_print_message(
			packet,
			packet_type,
			current_protocol_version,
			highest_supported_protocol_version,
			message,
			message_key,
			header,
			header_key,
			public_identity_key,
			public_ephemeral_key,
			public_prekey);
	if (status != 0) {
		goto cleanup;
	}

	//now decrypt the header
	status = packet_decrypt_header(
			packet,
			decrypted_header,
			decrypted_message_nonce,
			header_key,
			NULL,
			NULL,
			NULL);
	if (status != 0) {
		fprintf(stderr, "ERROR: Failed to decrypt header. (%i)\n", status);
		goto cleanup;
	}

	printf("Decrypted message nonce (%zu Bytes):\n", decrypted_message_nonce->content_length);
	print_hex(decrypted_message_nonce);
	putchar('\n');

	//now decrypt the message
	status = packet_decrypt_message(
			packet,
			decrypted_message,
			decrypted_message_nonce,
			message_key);
	if (status != 0) {
		fprintf(stderr, "ERROR: Failed to decrypt message. (%i)\n", status);
		goto cleanup;
	}

	//check the message size
	if (decrypted_message->content_length != message->content_length) {
		fprintf(stderr, "ERROR: Decrypted message length isn't the same.\n");
		status = EXIT_FAILURE;
		goto cleanup;
	}
	printf("Decrypted message length is the same.\n");

	//compare the message
	if (buffer_compare(message, decrypted_message) != 0) {
		fprintf(stderr, "ERROR: Decrypted message doesn't match!\n");
		status = EXIT_FAILURE;
		goto cleanup;
	}
	buffer_clear(decrypted_message);
	printf("Decrypted message is the same.\n");

cleanup:
	buffer_destroy_from_heap(header_key);
	buffer_destroy_from_heap(message_key);
	buffer_destroy_from_heap(header);
	buffer_destroy_from_heap(packet);
	buffer_destroy_from_heap(decrypted_header);
	buffer_destroy_from_heap(decrypted_message_nonce);
	buffer_destroy_from_heap(decrypted_message);
	buffer_destroy_from_heap(public_identity_key);
	buffer_destroy_from_heap(public_ephemeral_key);
	buffer_destroy_from_heap(public_prekey);
	return status;
}

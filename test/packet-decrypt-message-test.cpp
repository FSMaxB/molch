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
#include <exception>
#include <iostream>

#include "../lib/packet.hpp"
#include "../lib/molch.h"
#include "../lib/constants.h"
#include "../lib/molch-exception.hpp"
#include "utils.hpp"
#include "packet-test-lib.hpp"

using namespace Molch;

int main(void) {
	try {
		if (sodium_init() == -1) {
			throw MolchException(INIT_ERROR, "Failed to initialize libsodium.");
		}

		//generate keys and message
		Buffer message("Hello world!\n");
		Buffer header(4, 4);
		header.content[0] = 0x01;
		header.content[1] = 0x02;
		header.content[2] = 0x03;
		header.content[3] = 0x04;
		molch_message_type packet_type = NORMAL_MESSAGE;
		printf("Packet type: %02x\n", packet_type);
		putchar('\n');

		//NORMAL MESSAGE
		Buffer header_key(HEADER_KEY_SIZE, HEADER_KEY_SIZE);
		Buffer message_key(MESSAGE_KEY_SIZE, MESSAGE_KEY_SIZE);
		printf("NORMAL MESSAGE\n");
		Buffer packet;
		create_and_print_message(
			packet,
			header_key,
			message_key,
			packet_type,
			header,
			message,
			nullptr,
			nullptr,
			nullptr);

		//now decrypt the message
		Buffer decrypted_message = packet_decrypt_message(packet, message_key);

		//check the message size
		if (!decrypted_message.contains(message.size)) {
			throw MolchException(INVALID_VALUE, "Decrypted message length isn't the same.");
		}
		printf("Decrypted message length is the same.\n");

		//compare the message
		if (message != decrypted_message) {
			throw MolchException(INVALID_VALUE, "Decrypted message doesn't match.");
		}
		printf("Decrypted message is the same.\n\n");

		//manipulate the message
		packet.content[packet.size - crypto_secretbox_MACBYTES - 1] ^= 0xf0;
		printf("Manipulating message.\n");

		decrypted_message.clear();

		//try to decrypt
		bool decryption_failed = false;
		try {
			decrypted_message = packet_decrypt_message(packet, message_key);
		} catch (const MolchException& exception) {
			decryption_failed = true;
		}
		if (!decryption_failed) { //message was decrypted although it shouldn't
			throw MolchException(GENERIC_ERROR, "Decrypted manipulated message.");
		}
		printf("Manipulation detected.\n\n");

		//PREKEY MESSAGE
		printf("PREKEY MESSAGE\n");
		//create the public keys
		Buffer public_identity_key(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);
		public_identity_key.fillRandom(PUBLIC_KEY_SIZE);
		Buffer public_ephemeral_key(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);
		public_ephemeral_key.fillRandom(PUBLIC_KEY_SIZE);
		Buffer public_prekey(PUBLIC_KEY_SIZE, PUBLIC_KEY_SIZE);
		public_prekey.fillRandom(PUBLIC_KEY_SIZE);

		packet.clear();

		packet_type = PREKEY_MESSAGE;
		create_and_print_message(
			packet,
			header_key,
			message_key,
			packet_type,
			header,
			message,
			&public_identity_key,
			&public_ephemeral_key,
			&public_prekey);

		//now decrypt the message
		decrypted_message = packet_decrypt_message(packet, message_key);

		//check the message size
		if (!decrypted_message.contains(message.size)) {
			throw MolchException(INVALID_VALUE, "Decrypted message length isn't the same.");
		}
		printf("Decrypted message length is the same.\n");

		//compare the message
		if (message != decrypted_message) {
			throw MolchException(INVALID_VALUE, "Decrypted message doesn't match.");
		}
		printf("Decrypted message is the same.\n");
	} catch (const MolchException& exception) {
		exception.print(std::cerr) << std::endl;
		return EXIT_FAILURE;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

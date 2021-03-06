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
#include "molch/constants.h"
#include "utils.hpp"
#include "packet-test-lib.hpp"
#include "exception.hpp"

using namespace Molch;

int main() {
	try {
		TRY_VOID(Molch::sodium_init());

		//generate keys and message
		Buffer message{"Hello world!\n"};
		Buffer header{4, 4};
		header[0] = uchar_to_byte(0x01);
		header[1] = uchar_to_byte(0x02);
		header[2] = uchar_to_byte(0x03);
		header[3] = uchar_to_byte(0x04);
		molch_message_type packet_type{molch_message_type::NORMAL_MESSAGE};
		std::cout << "Packet type:" << static_cast<int>(packet_type) << '\n';
		putchar('\n');

		//NORMAL MESSAGE
		EmptyableHeaderKey header_key;
		MessageKey message_key;
		std::cout << "NORMAL MESSAGE\n";
		Buffer packet;
		create_and_print_message(
			packet,
			header_key,
			message_key,
			packet_type,
			header,
			message,
			std::nullopt);

		//now decrypt the message
		TRY_WITH_RESULT(normal_message_result, packet_decrypt_message(packet, message_key));
		const auto& normal_message{normal_message_result.value()};

		//check the message size
		if (normal_message.size() != message.size()) {
			throw Molch::Exception{status_type::INVALID_VALUE, "Decrypted message length isn't the same."};
		}
		std::cout << "Decrypted message length is the same.\n";

		//compare the message
		if (message != normal_message) {
			throw Molch::Exception{status_type::INVALID_VALUE, "Decrypted message doesn't match."};
		}
		std::cout << "Decrypted message is the same.\n\n";

		//manipulate the message
		packet[packet.size() - crypto_secretbox_MACBYTES - 1] ^= uchar_to_byte(0xf0);
		std::cout << "Manipulating message.\n";

		//try to decrypt
		const auto manipulated_normal_message_result = packet_decrypt_message(packet, message_key);
		if (manipulated_normal_message_result.has_value()) {
			throw Molch::Exception{status_type::GENERIC_ERROR, "Decrypted manipulated message."};
		}
		std::cout << "Manipulation detected.\n\n";

		//PREKEY MESSAGE
		std::cout << "PREKEY MESSAGE\n";
		//create the public keys
		auto prekey_metadata{std::make_optional<PrekeyMetadata>()};
		randombytes_buf(prekey_metadata.value().identity);
		randombytes_buf(prekey_metadata.value().ephemeral);
		randombytes_buf(prekey_metadata.value().prekey);

		packet.clear();

		packet_type = molch_message_type::PREKEY_MESSAGE;
		create_and_print_message(
			packet,
			header_key,
			message_key,
			packet_type,
			header,
			message,
			prekey_metadata);

		//now decrypt the message
		TRY_WITH_RESULT(prekey_message_result, packet_decrypt_message(packet, message_key));
		const auto& prekey_message{prekey_message_result.value()};

		//check the message size
		if (prekey_message.size() != message.size()) {
			throw Molch::Exception{status_type::INVALID_VALUE, "Decrypted message length isn't the same."};
		}
		std::cout << "Decrypted message length is the same.\n";

		//compare the message
		if (message != prekey_message) {
			throw Molch::Exception{status_type::INVALID_VALUE, "Decrypted message doesn't match."};
		}
		std::cout << "Decrypted message is the same.\n";
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

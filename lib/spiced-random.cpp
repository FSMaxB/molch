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

#include "constants.h"
#include "spiced-random.hpp"
#include "return-status.h"
#include "molch-exception.hpp"

/*
 * Generate a random number by combining the OSs random number
 * generator with an external source of randomness (like some kind of
 * user input).
 *
 * WARNING: Don't feed this with random numbers from the OSs random
 * source because it might annihilate the randomness.
 */
void spiced_random(
		Buffer& random_output,
		const Buffer& low_entropy_spice,
		const size_t output_length) {
	//check buffer length
	if (!random_output.fits(output_length)) {
		throw MolchException(INCORRECT_BUFFER_SIZE, "Output buffers is too short.");
	}

	//buffer that contains the random data from the OS
	Buffer os_random(output_length, output_length, &sodium_malloc, &sodium_free);
	os_random.fillRandom(output_length);

	//buffer that contains a random salt
	Buffer salt(crypto_pwhash_SALTBYTES, 0);
	salt.fillRandom(crypto_pwhash_SALTBYTES);

	//derive random data from the random spice
	Buffer spice(output_length, output_length, &sodium_malloc, &sodium_free);
	int status_int = crypto_pwhash(
			spice.content,
			spice.size,
			reinterpret_cast<const char*>(low_entropy_spice.content),
			low_entropy_spice.size,
			salt.content,
			crypto_pwhash_OPSLIMIT_INTERACTIVE,
			crypto_pwhash_MEMLIMIT_INTERACTIVE,
			crypto_pwhash_ALG_DEFAULT);
	if (status_int != 0) {
		throw MolchException(GENERIC_ERROR, "Failed to derive random data from spice.");
	}

	//now combine the spice with the OS provided random data.
	os_random.xorWith(spice);

	//copy the random data to the output
	random_output.cloneFrom(os_random);
}

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

#include "molch/constants.h"
#include "spiced-random.hpp"
#include "key.hpp"

namespace Molch {
	/*
	 * Generate a random number by combining the OSs random number
	 * generator with an external source of randomness (like some kind of
	 * user input).
	 *
	 * WARNING: Don't feed this with random numbers from the OSs random
	 * source because it might annihilate the randomness.
	 */
	result<SodiumBuffer> spiced_random(const span<const std::byte> low_entropy_spice, const size_t output_length) {
		FulfillOrFail((not low_entropy_spice.empty()) and (output_length != 0));

		//buffer that contains the random data from the OS
		SodiumBuffer os_random{output_length, output_length};
		randombytes_buf(os_random);

		//buffer that contains a random salt
		Key<crypto_pwhash_SALTBYTES,KeyType::Key> salt(uninitialized);
		randombytes_buf(salt);

		//derive random data from the random spice
		SodiumBuffer spice{output_length, output_length};
		OUTCOME_TRY(crypto_pwhash(
				spice,
				low_entropy_spice,
				salt,
				crypto_pwhash_OPSLIMIT_INTERACTIVE,
				crypto_pwhash_MEMLIMIT_INTERACTIVE,
				crypto_pwhash_ALG_DEFAULT));

		//now combine the spice with the OS provided random data.
		auto spice_iterator{std::cbegin(spice)};
		for (auto& byte : os_random) {
			byte ^= *spice_iterator;
			++spice_iterator;
		}

		return os_random;
	}
}

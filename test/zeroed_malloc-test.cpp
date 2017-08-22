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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

#include "../lib/zeroed_malloc.hpp"
#include "utils.hpp"

using namespace Molch;

int main(void) noexcept {
	return_status status = return_status_init();

	unsigned char * const pointer = reinterpret_cast<unsigned char*>(zeroed_malloc(100));
	if (pointer == nullptr) {
		THROW(ALLOCATION_FAILED, "Failed to allocate with zeroed_malloc.");
	}

	printf("Checking size.\n");
	{
		size_t size = 0;
		std::copy(pointer - sizeof(size_t), pointer, reinterpret_cast<unsigned char*>(&size));
		size_t expected_elements = (100 / sizeof(max_align_t));
		if ((100 % sizeof(max_align_t)) != 0) {
			++expected_elements;
		}
		if (size != (expected_elements * sizeof(max_align_t))) {
			THROW(INCORRECT_DATA, "Size stored in the memory location is incorrect.");
		}
		printf("size = %zu\n", size);
	}

	printf("Checking pointer.\n");
	{
		unsigned char *pointer_copy = nullptr;
		std::copy(pointer - sizeof(size_t) - sizeof(void*), pointer - sizeof(size_t), reinterpret_cast<unsigned char*>(&pointer_copy));
		printf("pointer_copy = %p\n", reinterpret_cast<void*>(pointer_copy));
	}

	zeroed_free(pointer);

	{
		void *new_pointer = protobuf_c_allocator(nullptr, 20);
		protobuf_c_free(nullptr, new_pointer);
	}

cleanup:
	on_error {
		print_errors(status);
	}
	return_status_destroy_errors(&status);

	return status.status;
}

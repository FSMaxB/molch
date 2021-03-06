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

#include "protobuf.hpp"
#include "gsl.hpp"

namespace Molch {
	void EncryptedBackupDeleter::operator ()(ProtobufCEncryptedBackup* backup) {
		molch__protobuf__encrypted_backup__free_unpacked(backup, &protobuf_c_allocator);
	}

	void HeaderDeleter::operator ()(ProtobufCHeader* header) {
		molch__protobuf__header__free_unpacked(header, &protobuf_c_allocator);
	}

	void PacketDeleter::operator ()(ProtobufCPacket *packet) {
		molch__protobuf__packet__free_unpacked(packet, &protobuf_c_allocator);
	}

	void *protobuf_c_new(void *allocator_data, size_t size) {
		(void)allocator_data;
		return reinterpret_cast<void*>(new std::byte[size]); //NOLINT
	}
	void protobuf_c_delete(void *allocator_data, void *pointer) {
		(void)allocator_data;
		delete[] reinterpret_cast<std::byte*>(pointer); //NOLINT
	}

	ProtobufCAllocator protobuf_c_allocator = {
		&protobuf_c_new,
		&protobuf_c_delete,
		nullptr
	};
}

/*
 * Molch, an implementation of the axolotl ratchet based on libsodium
 *
 * ISC License
 *
 * Copyright (C) 2015-2016 Max Bruckner (FSMaxB) <max at maxbruckner dot de>
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

#ifndef LIB_BUFFER_H
#define LIB_BUFFER_H

#include <cstdbool>
#include <cstdlib>
#include <ostream>
#include <memory>
#include <exception>
#include <sodium.h>
#include <algorithm>
#include <iterator>

#include "gsl.hpp"
#include "molch-exception.hpp"

namespace Molch {
	class Buffer {
	private:
		size_t buffer_length{0};
		bool manage_memory{true}; //should the destructor manage memory?
		bool readonly{false}; //if set, this buffer shouldn't be written to.
		void (*deallocator)(void*){nullptr}; //a deallocator if the buffer has been allocated with a custom allocator

		size_t content_length{0};
		gsl::byte *content{nullptr};

		/* implementation of copy construction and assignment */
		Buffer& copy(const Buffer& buffer) {
			this->destruct();

			this->buffer_length = buffer.buffer_length;
			this->manage_memory = true;
			this->readonly = buffer.readonly;
			this->deallocator = nullptr;
			this->content_length = buffer.content_length;

			this->content = new gsl::byte[buffer.buffer_length];
			std::copy(buffer.content, buffer.content + buffer.content_length, this->content);

			return *this;
		}

		/* implementation of move construction and assignment */
		Buffer& move(Buffer&& buffer) {
			this->destruct();

			//copy the buffer
			auto& source_reference{reinterpret_cast<gsl::byte&>(buffer)};
			auto& destination_reference{reinterpret_cast<gsl::byte&>(*this)};
			std::copy(&source_reference, &source_reference + sizeof(Buffer), &destination_reference);

			//steal resources from the source buffer
			buffer.buffer_length = 0;
			buffer.manage_memory = false;
			buffer.readonly = false;
			buffer.deallocator = nullptr;
			buffer.content_length = 0;
			buffer.content = nullptr;

			return *this;
		}

		/*
		 * Deallocate all dynamically allocated memory
		 */
		void destruct() {
			//only do something if this was created using a constructor
			if (this->manage_memory) {
				this->clear();
				if ((this->deallocator != nullptr) && (this->content != nullptr)) {
					deallocator(this->content);
					return;
				}

				delete[] this->content;
			}
		}

	public:
		Buffer() = default; // does nothing
		/* move constructor */
		Buffer(Buffer&& buffer) {
			this->move(std::move(buffer));
		}

		/* copy constructor */
		Buffer(const Buffer& buffer) {
			this->copy(buffer);
		}

		Buffer(const std::string& string) :
				buffer_length{string.length() + sizeof("")},
				content_length{string.length() + sizeof("")},
				content{new gsl::byte[string.length() + sizeof("")]} {
			std::copy(std::begin(string), std::end(string), reinterpret_cast<char*>(this->content));
			this->content[string.length()] = static_cast<gsl::byte>('\0');
		}

		Buffer(const size_t capacity, const size_t size) :
				buffer_length{capacity},
				content_length{size} {
			if (capacity == 0) {
				this->content = nullptr;
			} else {
				this->content = new gsl::byte[capacity];
			}
		}

		Buffer(const size_t capacity, const size_t size, void* (*allocator)(size_t), void (*deallocator)(void*)) :
				buffer_length{capacity},
				deallocator{deallocator},
				content_length{size} {
			if (capacity == 0) {
				this->content = nullptr;
			} else {
				this->content = reinterpret_cast<gsl::byte*>(allocator(capacity));
				if (this->content == nullptr) {
					this->buffer_length = 0;
					this->content_length = 0;

					throw std::bad_alloc{};
				}
			}
		}
		~Buffer() {
			this->destruct();
		}

		//move assignment
		Buffer& operator=(Buffer&& buffer) {
			return this->move(std::move(buffer));
		}
		//copy assignment
		Buffer& operator=(const Buffer& buffer) {
			return this->copy(buffer);
		}

		gsl::byte& operator[](size_t index) {
			Expects(index < this->content_length);

			return this->content[index];
		}
		const gsl::byte& operator[](size_t index) const {
			Expects(index < this->content_length);

			return this->content[index];
		}

		gsl::byte* data() noexcept {
			return this->content;
		}
		const gsl::byte* data() const noexcept {
			return this->content;
		}

		size_t size() const noexcept {
			return this->content_length;
		}

		bool empty() const noexcept {
			return this->content_length == 0;
		}

		void setSize(size_t size) {
			Expects(size <= this->buffer_length);

			this->content_length = size;
		}

		gsl::byte* begin() noexcept {
			return this->content;
		}
		const gsl::byte* begin() const noexcept {
			return this->content;
		}
		gsl::byte* end() noexcept {
			return this->content + this->content_length;
		}
		const gsl::byte* end() const noexcept {
			return this->content + this->content_length;
		}

		/*
		 * Clear a buffer.
		 *
		 * Overwrites the buffer with zeroes and
		 * resets the content size.
		 */
		void clear() noexcept {
			if (this->buffer_length == 0) {
				return;
			}
			sodium_memzero(this->content, this->buffer_length);
			this->content_length = 0;
		}

		/*
		 * Xor another buffer with the same length onto this one.
		 */
		void xorWith(const Buffer& source) {
			Expects(!this->readonly && (this->content_length == source.content_length));

			//xor source onto destination
			for (size_t i{0}; i < this->content_length; i++) {
				this->content[i] ^= source.content[i];
			}
		}

		/*
		 * Fill a buffer with random numbers.
		 */
		void fillRandom(const size_t length) {
			Expects((length <= this->buffer_length)
					&& !this->readonly);

			if (this->buffer_length == 0) {
				return;
			}

			this->content_length = length;
			randombytes_buf(this->content, length);
		}

		/*
		 * Compare two buffers.
		 *
		 * Returns 0 if both buffers match.
		 */
		int compare(const Buffer& buffer) const {
			return this->compareToRaw({buffer.content, narrow(buffer.content_length)});
		}

		/*
		 * Compare parts of two buffers.
		 *
		 * Returns 0 if both buffers match.
		 */
		int comparePartial(
				const size_t position1,
				const Buffer& buffer2,
				const size_t position2,
				const size_t length) const {
			return this->compareToRawPartial(position1, {buffer2.content, narrow(buffer2.content_length)}, position2, length);
		}

		/*
		 * Compare a buffer to a raw array.
		 *
		 * Returns 0 if both buffers match.
		 */
		int compareToRaw(const gsl::span<const gsl::byte> array) const {
			return this->compareToRawPartial(0, array, 0, this->content_length);
		}


		/*
		 * Compare parts of a buffer to parts of a raw array.
		 *
		 * Returns 0 if both buffers match.
		 */
		int compareToRawPartial(
				const size_t position1,
				const gsl::span<const gsl::byte> array,
				const size_t position2,
				const size_t comparison_length) const {
			if (((this->content_length - position1) < comparison_length) || ((narrow(array.size()) - position2) < comparison_length)) {
				//FIXME: Does this introduce a timing sidechannel? This leaks the information that two buffers don't have the same length
				//buffers are too short
				return -6; //TODO: Is this an exception?
			}

			if ((this->buffer_length == 0) || (array.empty())) {
				if (comparison_length == 0) {
					return 0;
				} else {
					return -1;
				}
			}

			return sodium_memcmp(this->content + position1, array.data() + position2, comparison_length);
		}

		/*
		 * Copy parts of a buffer to another buffer.
		 */
		void copyFrom(
				const size_t destination_offset,
				const Buffer& source,
				const size_t source_offset,
				const size_t copy_length) {
			Expects(!this->readonly
					&& (this->buffer_length >= this->content_length)
					&& (source.buffer_length >= source.content_length)
					&& (destination_offset <= this->content_length)
					&& (copy_length <= (this->buffer_length - destination_offset))
					&& (source_offset <= source.content_length)
					&& (copy_length <= (source.content_length - source_offset))
					&& ((this->content_length == 0) || (this->content != nullptr))
					&& ((source.content_length == 0) || (source.content != nullptr)));

			if (source.buffer_length == 0) {
				return; //nothing to do
			}

			std::copy(source.content + source_offset, source.content + source_offset + copy_length, this->content + destination_offset);
			this->content_length = (this->content_length > destination_offset + copy_length)
				? this->content_length
				: destination_offset + copy_length;
		}

		/*
		 * Copy the content of a buffer to the beginning of another
		 * buffer and set the destinations content length to the
		 * same length as the source.
		 */
		void cloneFrom(const Buffer& source) {
			Expects(!this->readonly && (this->buffer_length >= source.content_length));

			this->content_length = source.content_length;

			this->copyFrom(0, source, 0, source.content_length);
		}

		/*
		 * Copy from a raw array to a buffer.
		 */
		void copyFromRaw(
				const size_t destination_offset,
				const gsl::byte * const source,
				const size_t source_offset,
				const size_t copy_length) {
			Expects(!this->readonly
					&& (this->buffer_length >= destination_offset)
					&& (copy_length <= (this->buffer_length - destination_offset)));

			if (copy_length == 0) {
				return;
			}

			std::copy(source + source_offset, source + source_offset + copy_length, this->content + destination_offset);
			this->content_length = (this->content_length > destination_offset + copy_length)
				? this->content_length
				: destination_offset + copy_length;
		}

		/*
		 * Copy the content of a raw array to the
		 * beginning of a buffer, setting the buffers
		 * content length to the length that was copied.
		 */
		void cloneFromRaw(const gsl::span<const gsl::byte> source) {
			Expects(!this->readonly && (this->buffer_length >= narrow(source.size())));

			this->content_length = narrow(source.size());

			this->copyFromRaw(0, source.data(), 0, narrow(source.size()));
		}

		/*
		 * Copy from a buffer to a raw array.
		 */
		void copyToRaw(
				gsl::byte * const destination,
				const size_t destination_offset,
				const size_t source_offset,
				const size_t copy_length) const {
			Expects((source_offset <= this->content_length) && (copy_length <= (this->content_length - source_offset))
					&& (this->buffer_length >= this->content_length));

			if (this->buffer_length == 0) {
				return;
			}

			std::copy(this->content + source_offset, this->content + source_offset + copy_length, destination + destination_offset);
		}

		/*
		 * Copy the entire content of a buffer
		 * to a raw array.
		 */
		void cloneToRaw(const gsl::span<gsl::byte> destination) const {
			Expects(narrow(destination.size()) >= this->content_length);

			this->copyToRaw(destination.data(), 0, 0, this->content_length);
		}

		/*
		 * Return the content and set the capacity to 0 and size to 0.
		 */
		gsl::byte* release() {
			auto content{this->content};
			this->content = nullptr;
			this->content_length = 0;
			this->buffer_length = 0;

			return content;
		}

		gsl::span<gsl::byte> span() {
			return {this->content, narrow(this->content_length)};
		}
		gsl::span<const gsl::byte> span() const {
			return {this->content, narrow(this->content_length)};
		}

		std::ostream& print(std::ostream& stream) const {
			stream << std::string(reinterpret_cast<char*>(this->content), this->content_length);

			return stream;
		}

		std::ostream& printHex(std::ostream& stream) const {
			static const int width{30};
			//buffer for the hex string
			const size_t hex_length{this->content_length * 2 + sizeof("")};
			auto hex{std::make_unique<char[]>(hex_length)};
			if (sodium_bin2hex(hex.get(), hex_length, byte_to_uchar(this->content), this->content_length) == nullptr) {
				throw Exception{status_type::BUFFER_ERROR, "Failed to convert binary to hex with sodium_bin2hex."};
			}

			for (size_t i{0}; i < hex_length; i++) {
				if ((width != 0) && ((i % width) == 0) && (i != 0)) {
					stream << '\n';
				} else if ((i % 2 == 0) && (i != 0)) {
					stream << ' ';
				}
				stream << hex[i];
			}

			return stream;
		}

		bool operator ==(const Buffer& buffer) const {
			return this->compare(buffer) == 0;
		}
		bool operator !=(const Buffer& buffer) const {
			return !(*this == buffer);
		}

		bool isNone() const noexcept {
			return (this->content_length == 0) || sodium_is_zero(byte_to_uchar(this->content), this->content_length);
		}

		size_t capacity() const noexcept {
			return this->buffer_length;
		}

		void setReadOnly() noexcept {
			this->readonly = true;
		}

		bool fits(const size_t size) const {
			return this->buffer_length >= size;
		}
		bool contains(const size_t size) const {
			return this->fits(size) && (this->content_length == size);
		}
	};
}
#endif

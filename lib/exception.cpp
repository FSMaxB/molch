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
#include <new>
#include <memory>
#include <iterator>
#include <sstream>

#include "exception.hpp"

namespace Molch {
	Exception::Exception(const Error& error) {
		this->add(error);
	}

	Exception::Exception(const status_type type, const std::string& message) {
		this->error_stack.push_front(Error(type, message));
	}

	Exception::Exception(return_status& status) {
		auto *error{status.error};
		while (error != nullptr) {
			this->error_stack.emplace_back(error->status, error->message);
			error = error->next;
		}

		return_status_destroy_errors(status);
	}

	const char* Exception::what() const noexcept {
		if (this->printed.empty()) {
			std::stringstream sstream;
			this->print(sstream);
			this->printed = sstream.str();
		}

		return this->printed.c_str();
	}

	Exception& Exception::add(const Exception& exception) {
		for (const auto& error : exception.error_stack) {
			this->error_stack.push_back(error);
		}

		return *this;
	}

	Exception& Exception::add(const Error& error) {
		this->error_stack.push_front(error);

		return *this;
	}

	return_status Exception::toReturnStatus() const {
		auto status{return_status_init()};

		// add the error messages in reverse order
		for (auto&& error = std::crbegin(this->error_stack); error != std::crend(this->error_stack); ++error) {
			auto add_status{return_status_add_error_message(status, error->message.c_str(), error->type)};
			if (add_status != status_type::SUCCESS) {
				return_status_destroy_errors(status);
				status.status = add_status;
				return status;
			}
		}

		return status;
	}

	std::ostream& Exception::print(std::ostream& stream) const {
		stream << "ERROR\nerror stack trace:\n";

		size_t i{0};
		for (const auto& error : this->error_stack) {
			stream << i << ": " << return_status_get_name(error.type) << ", " << error.message << '\n';
			i++;
		}

		return stream;
	}
}
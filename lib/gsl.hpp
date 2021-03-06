/*
 * \file Loads the CPP Core Guidelines support libraries and configures it's behavior.
 */

#ifndef LIB_GSL_HPP
#define LIB_GSL_HPP

#define GSL_THROW_ON_CONTRACT_VIOLATION //throw exception when contract is violated (instead of std::terminate)!

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wno-tautological-constant-compare"
#include <gsl/gsl_assert>
#include <gsl/gsl_byte>
#include <gsl/span>
#pragma GCC diagnostic pop

#include "protobuf.hpp"

namespace Molch {
	inline unsigned char* byte_to_uchar(std::byte* byte) {
		return reinterpret_cast<unsigned char*>(byte); //NOLINT
	}

	inline const unsigned char* byte_to_uchar(const std::byte* byte) {
		return reinterpret_cast<const unsigned char*>(byte); //NOLINT
	}

	constexpr unsigned char byte_to_uchar(const std::byte byte) {
		return static_cast<unsigned char>(byte);
	}

	inline std::byte* uchar_to_byte(unsigned char* character) {
		return reinterpret_cast<std::byte*>(character); //NOLINT
	}

	inline const std::byte* uchar_to_byte(const unsigned char* character) {
		return reinterpret_cast<const std::byte*>(character); //NOLINT
	}

	constexpr std::byte uchar_to_byte(const unsigned char character) {
		return static_cast<std::byte>(character);
	}

	inline char* byte_to_char(std::byte* pointer) noexcept {
		return reinterpret_cast<char*>(pointer); //NOLINT
	}

	inline const char* byte_to_char(const std::byte* pointer) noexcept {
		return reinterpret_cast<const char*>(pointer); //NOLINT
	}

	inline const std::byte* char_to_byte(const char* pointer) noexcept {
		return reinterpret_cast<const std::byte*>(pointer); //NOLINT
	}

	inline std::byte* char_to_byte(char* pointer) noexcept {
		return reinterpret_cast<std::byte*>(pointer); //NOLINT
	}

	template <class ElementType,std::ptrdiff_t length = -1>
	class span : public gsl::span<ElementType,length> {
	private:
		using base_class = gsl::span<ElementType,length>;

	public:
		//make base class constructors available
		using base_class::base_class;

		constexpr span() : base_class{nullptr, static_cast<ptrdiff_t>(0)} {}

		constexpr span(ElementType* pointer, size_t count)
			: base_class{pointer, gsl::narrow<ptrdiff_t>(count)} {}

		constexpr span(gsl::span<ElementType> gsl_span) : base_class{gsl_span} {}

		constexpr size_t size() const {
			return gsl::narrow<size_t>(this->base_class::size());
		}

		span(const ProtobufCBinaryData data) : base_class{uchar_to_byte(data.data), gsl::narrow<ptrdiff_t>(data.len)} {}

		const span subspan(size_t offset, size_t size) const {
			const auto signed_offset = gsl::narrow_cast<ptrdiff_t>(offset);
			const auto signed_size = gsl::narrow_cast<ptrdiff_t>(size);
			return this->base_class::subspan(signed_offset, signed_size);
		}

		const span subspan(size_t offset) const {
			const auto signed_offset = gsl::narrow_cast<ptrdiff_t>(offset);
			return this->base_class::subspan(signed_offset);
		}
	};
}

#endif /* LIB_GSL_HPP */

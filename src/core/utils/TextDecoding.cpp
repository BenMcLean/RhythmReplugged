#include "core/utils/TextDecoding.h"

#include <stdexcept>

namespace rhythmreplugged::core::utils
{
	namespace
	{
		void append_utf8_codepoint(std::string &output, char32_t codepoint)
		{
			if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
				throw std::range_error("Invalid Unicode code point");

			if (codepoint <= 0x7F)
			{
				output.push_back(static_cast<char>(codepoint));
				return;
			}

			if (codepoint <= 0x7FF)
			{
				output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
				output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
				return;
			}

			if (codepoint <= 0xFFFF)
			{
				output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
				output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
				output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
				return;
			}

			output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
			output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
			output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		}

		std::optional<std::string> utf16_to_utf8(const std::u16string &utf16)
		{
			std::string output;
			output.reserve(utf16.size() * 3);

			try
			{
				for (size_t i = 0; i < utf16.size(); ++i)
				{
					const char16_t unit = utf16[i];
					if (unit >= 0xD800 && unit <= 0xDBFF)
					{
						if (i + 1 >= utf16.size())
							throw std::range_error("Truncated UTF-16 surrogate pair");

						const char16_t low = utf16[++i];
						if (low < 0xDC00 || low > 0xDFFF)
							throw std::range_error("Invalid UTF-16 surrogate pair");

						const char32_t codepoint = 0x10000 +
							((static_cast<char32_t>(unit - 0xD800) << 10) |
								static_cast<char32_t>(low - 0xDC00));
						append_utf8_codepoint(output, codepoint);
						continue;
					}

					if (unit >= 0xDC00 && unit <= 0xDFFF)
						throw std::range_error("Unexpected UTF-16 low surrogate");

					append_utf8_codepoint(output, static_cast<char32_t>(unit));
				}
			}
			catch (const std::range_error &)
			{
				return std::nullopt;
			}

			return output;
		}

		std::optional<std::string> utf32_to_utf8(const std::u32string &utf32)
		{
			std::string output;
			output.reserve(utf32.size() * 4);

			try
			{
				for (const char32_t codepoint : utf32)
					append_utf8_codepoint(output, codepoint);
			}
			catch (const std::range_error &)
			{
				return std::nullopt;
			}

			return output;
		}
	}

	std::optional<std::string> decode_text_bytes(const std::vector<char> &bytes)
	{
		if (bytes.empty())
			return std::string();

		const auto *raw = reinterpret_cast<const unsigned char *>(bytes.data());
		const size_t size = bytes.size();

		if (size >= 4 && raw[0] == 0xFF && raw[1] == 0xFE && raw[2] == 0x00 && raw[3] == 0x00)
		{
			std::u32string utf32;
			utf32.reserve((size - 4) / 4);
			for (size_t i = 4; i + 3 < size; i += 4)
			{
				const char32_t codepoint = static_cast<char32_t>(raw[i]) |
					(static_cast<char32_t>(raw[i + 1]) << 8) |
					(static_cast<char32_t>(raw[i + 2]) << 16) |
					(static_cast<char32_t>(raw[i + 3]) << 24);
				utf32.push_back(codepoint);
			}

			return utf32_to_utf8(utf32);
		}

		if (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE)
		{
			std::u16string utf16;
			utf16.reserve((size - 2) / 2);
			for (size_t i = 2; i + 1 < size; i += 2)
			{
				const char16_t codeunit = static_cast<char16_t>(raw[i]) |
					(static_cast<char16_t>(raw[i + 1]) << 8);
				utf16.push_back(codeunit);
			}

			return utf16_to_utf8(utf16);
		}

		if (size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF)
		{
			std::u16string utf16;
			utf16.reserve((size - 2) / 2);
			for (size_t i = 2; i + 1 < size; i += 2)
			{
				const char16_t codeunit = static_cast<char16_t>((raw[i] << 8) | raw[i + 1]);
				utf16.push_back(codeunit);
			}

			return utf16_to_utf8(utf16);
		}

		if (size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF)
			return std::string(bytes.begin() + 3, bytes.end());

		return std::string(bytes.begin(), bytes.end());
	}
}

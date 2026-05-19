#include "frontend_contract/FrontendOptions.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace rhythmreplugged::frontend_contract
{
	namespace
	{
		std::string_view trim_ascii_whitespace(std::string_view text)
		{
			size_t start = 0;
			while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
				++start;

			size_t end = text.size();
			while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
				--end;

			return text.substr(start, end - start);
		}

		bool parse_quoted_value(std::string_view text, std::string &value)
		{
			const std::string_view trimmed = trim_ascii_whitespace(text);
			if (trimmed.size() < 2 || trimmed.front() != '"' || trimmed.back() != '"')
				return false;

			value.clear();
			value.reserve(trimmed.size() - 2);
			for (size_t index = 1; index + 1 < trimmed.size(); ++index)
			{
				const char ch = trimmed[index];
				if (ch == '\\' && index + 1 < trimmed.size() - 1)
				{
					const char escaped = trimmed[++index];
					switch (escaped)
					{
					case '\\':
					case '"':
						value.push_back(escaped);
						break;
					case 'n':
						value.push_back('\n');
						break;
					case 'r':
						value.push_back('\r');
						break;
					case 't':
						value.push_back('\t');
						break;
					default:
						value.push_back(escaped);
						break;
					}
					continue;
				}

				value.push_back(ch);
			}

			return true;
		}

		std::string escape_quoted_value(std::string_view value)
		{
			std::string escaped;
			escaped.reserve(value.size() + 2);
			for (const char ch : value)
			{
				switch (ch)
				{
				case '\\':
				case '"':
					escaped.push_back('\\');
					escaped.push_back(ch);
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}

			return escaped;
		}
	}

	std::string_view frontend_option_value(const FrontendOptions &options, FrontendOptionId id)
	{
		switch (id)
		{
		case FrontendOptionId::DefaultInstrument:
			return options.default_instrument.empty() ? std::string_view("ask") : std::string_view(options.default_instrument);
		case FrontendOptionId::DefaultDifficulty:
			return options.default_difficulty.empty() ? std::string_view("ask") : std::string_view(options.default_difficulty);
		case FrontendOptionId::MultithreadedFileLoading:
			return options.multithreaded_file_loading ? std::string_view("enabled") : std::string_view("disabled");
		}

		return {};
	}

	bool copy_frontend_option_value(FrontendOptions &destination, const FrontendOptions &source, FrontendOptionId id)
	{
		const FrontendOptionDefinition *definition = find_frontend_option_by_id(id);
		return definition != nullptr && set_frontend_option_value(destination, id, frontend_option_value(source, id));
	}

	bool set_frontend_option_value_by_key(FrontendOptions &options, std::string_view key, std::string_view value)
	{
		const FrontendOptionDefinition *definition = find_frontend_option_by_libretro_key(key);
		return definition != nullptr && set_frontend_option_value(options, definition->id, value);
	}

	bool parse_frontend_options_config(std::string_view text, FrontendOptions &options)
	{
		bool all_valid = true;
		size_t start = 0;
		while (start <= text.size())
		{
			const size_t line_end = text.find_first_of("\r\n", start);
			const std::string_view raw_line = text.substr(start, line_end == std::string_view::npos ? text.size() - start : line_end - start);
			const std::string_view line = trim_ascii_whitespace(raw_line);
			if (!line.empty() && line.front() != '#')
			{
				const size_t equals = line.find('=');
				if (equals == std::string_view::npos)
				{
					all_valid = false;
				}
				else
				{
					const std::string_view key = trim_ascii_whitespace(line.substr(0, equals));
					const std::string_view value_text = line.substr(equals + 1);
					std::string value;
					if (!parse_quoted_value(value_text, value) || !set_frontend_option_value_by_key(options, key, value))
						all_valid = false;
				}
			}

			if (line_end == std::string_view::npos)
				break;

			start = line_end + 1;
			if (line_end + 1 < text.size() && text[line_end] == '\r' && text[line_end + 1] == '\n')
				++start;
		}

		return all_valid;
	}

	std::string serialize_frontend_options_config(const FrontendOptions &options)
	{
		std::string text;
		for (const FrontendOptionDefinition &definition : frontend_option_definitions())
		{
			text += definition.libretro_key;
			text += " = \"";
			text += escape_quoted_value(frontend_option_value(options, definition.id));
			text += "\"\n";
		}

		return text;
	}
}

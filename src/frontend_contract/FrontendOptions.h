#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace rhythmreplugged::frontend_contract
{
	enum class FrontendOptionId
	{
		DefaultInstrument,
		DefaultDifficulty,
		MultithreadedFileLoading,
	};

	struct FrontendOptionChoice
	{
		const char *value = nullptr;
		const char *label = nullptr;
	};

	struct FrontendOptionDefinition
	{
		FrontendOptionId id = FrontendOptionId::DefaultInstrument;
		const char *command_line_flag = nullptr;
		const char *libretro_key = nullptr;
		const char *display_name = nullptr;
		const char *description = nullptr;
		const FrontendOptionChoice *choices = nullptr;
		size_t choice_count = 0;
		const char *default_value = nullptr;
	};

	struct FrontendOptions
	{
		std::string default_instrument;
		std::string default_difficulty;
		bool multithreaded_file_loading = true;
	};

	inline constexpr std::array<FrontendOptionChoice, 6> kFrontendInstrumentChoices{{
		{"ask", "Ask"},
		{"guitar", "Guitar"},
		{"bass", "Bass"},
		{"rhythm", "Rhythm"},
		{"coop-guitar", "Co-op Guitar"},
		{"keys", "Keys"},
	}};

	inline constexpr std::array<FrontendOptionChoice, 5> kFrontendDifficultyChoices{{
		{"ask", "Ask"},
		{"easy", "Easy"},
		{"medium", "Medium"},
		{"hard", "Hard"},
		{"expert", "Expert"},
	}};

	inline constexpr std::array<FrontendOptionChoice, 2> kFrontendMultithreadedFileLoadingChoices{{
		{"enabled", "Enabled"},
		{"disabled", "Disabled"},
	}};

	inline constexpr std::array<FrontendOptionDefinition, 3> kFrontendOptionDefinitions{{
		{
			FrontendOptionId::DefaultInstrument,
			"--instrument",
			"rhythmreplugged_instrument",
			"Startup Instrument",
			"Preferred instrument when starting a song directly. Ask shows the instrument menu unless only one valid instrument exists.",
			kFrontendInstrumentChoices.data(),
			kFrontendInstrumentChoices.size(),
			"ask",
		},
		{
			FrontendOptionId::DefaultDifficulty,
			"--difficulty",
			"rhythmreplugged_difficulty",
			"Startup Difficulty",
			"Preferred difficulty when starting a song directly. Ask shows the difficulty menu unless only one valid difficulty exists.",
			kFrontendDifficultyChoices.data(),
			kFrontendDifficultyChoices.size(),
			"ask",
		},
		{
			FrontendOptionId::MultithreadedFileLoading,
			"--multithreaded-file-loading",
			"rhythmreplugged_multithreaded_file_loading",
			"Multithreaded File Loading",
			"Controls whether song stems are loaded on multiple worker threads. Disable this for frontends whose file I/O callbacks are not safe to call concurrently.",
			kFrontendMultithreadedFileLoadingChoices.data(),
			kFrontendMultithreadedFileLoadingChoices.size(),
			"enabled",
		},
	}};

	inline std::span<const FrontendOptionDefinition> frontend_option_definitions()
	{
		return kFrontendOptionDefinitions;
	}

	inline const FrontendOptionDefinition *find_frontend_option_by_command_line_flag(std::string_view flag)
	{
		for (const FrontendOptionDefinition &definition : kFrontendOptionDefinitions)
		{
			if (flag == definition.command_line_flag)
				return &definition;
		}

		return nullptr;
	}

	inline bool frontend_option_accepts_value(const FrontendOptionDefinition &definition, std::string_view value)
	{
		for (size_t index = 0; index < definition.choice_count; ++index)
		{
			if (value == definition.choices[index].value)
				return true;
		}

		return false;
	}

	inline bool set_frontend_option_value(FrontendOptions &options, FrontendOptionId id, std::string_view value)
	{
		const FrontendOptionDefinition *definition = nullptr;
		for (const FrontendOptionDefinition &candidate : kFrontendOptionDefinitions)
		{
			if (candidate.id == id)
			{
				definition = &candidate;
				break;
			}
		}

		if (definition == nullptr || !frontend_option_accepts_value(*definition, value))
			return false;

		switch (id)
		{
		case FrontendOptionId::DefaultInstrument:
			options.default_instrument = value == "ask" ? std::string() : std::string(value);
			return true;
		case FrontendOptionId::DefaultDifficulty:
			options.default_difficulty = value == "ask" ? std::string() : std::string(value);
			return true;
		case FrontendOptionId::MultithreadedFileLoading:
			options.multithreaded_file_loading = value != "disabled";
			return true;
		}

		return false;
	}
}

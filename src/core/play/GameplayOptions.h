#pragma once

#include <vector>

namespace rhythmreplugged::core
{
	enum class GameplayMode
	{
		Classic,
		Freeplay,
		Replugged,
	};

	enum class DifficultyOption
	{
		Easy,
		Medium,
		Hard,
		Expert,
	};

	enum class InstrumentOption
	{
		Guitar,
		Bass,
		Rhythm,
		CoopGuitar,
		Keys,
		Drums,
	};

	class GameplayOptions
	{
	public:
		GameplayMode gameplay_mode() const;
		void set_gameplay_mode(GameplayMode gameplay_mode);
		DifficultyOption difficulty() const;
		void set_difficulty(DifficultyOption difficulty);
		InstrumentOption instrument() const;
		void set_instrument(InstrumentOption instrument);
		const std::vector<InstrumentOption> &claimed_instruments() const;
		void set_claimed_instruments(std::vector<InstrumentOption> instruments);
		bool has_claimed_instrument(InstrumentOption instrument) const;
		bool toggle_claimed_instrument(InstrumentOption instrument);
		const std::vector<InstrumentOption> &reserved_instruments() const;
		void set_reserved_instruments(std::vector<InstrumentOption> instruments);

	private:
		GameplayMode gameplay_mode_ = GameplayMode::Classic;
		DifficultyOption difficulty_ = DifficultyOption::Medium;
		InstrumentOption instrument_ = InstrumentOption::Guitar;
		std::vector<InstrumentOption> claimed_instruments_{InstrumentOption::Guitar};
		std::vector<InstrumentOption> reserved_instruments_;
	};
}

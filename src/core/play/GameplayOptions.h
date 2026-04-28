#pragma once

namespace rhythmreplugged::core
{
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
		DifficultyOption difficulty() const;
		void set_difficulty(DifficultyOption difficulty);
		InstrumentOption instrument() const;
		void set_instrument(InstrumentOption instrument);

	private:
		DifficultyOption difficulty_ = DifficultyOption::Medium;
		InstrumentOption instrument_ = InstrumentOption::Guitar;
	};
}

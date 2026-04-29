#pragma once

namespace rhythmreplugged::core
{
	enum class GameplayMode
	{
		Classic,
		Freeplay,
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

	private:
		GameplayMode gameplay_mode_ = GameplayMode::Classic;
		DifficultyOption difficulty_ = DifficultyOption::Medium;
		InstrumentOption instrument_ = InstrumentOption::Guitar;
	};
}

#include "core/play/GameplayOptions.h"

namespace rhythmreplugged::core
{
	GameplayMode GameplayOptions::gameplay_mode() const
	{
		return gameplay_mode_;
	}

	void GameplayOptions::set_gameplay_mode(GameplayMode gameplay_mode)
	{
		gameplay_mode_ = gameplay_mode;
	}

	DifficultyOption GameplayOptions::difficulty() const
	{
		return difficulty_;
	}

	void GameplayOptions::set_difficulty(DifficultyOption difficulty)
	{
		difficulty_ = difficulty;
	}

	InstrumentOption GameplayOptions::instrument() const
	{
		return instrument_;
	}

	void GameplayOptions::set_instrument(InstrumentOption instrument)
	{
		instrument_ = instrument;
	}
}

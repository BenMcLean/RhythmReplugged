#include "core/play/GameplayOptions.h"

namespace rhythmreplugged::core
{
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

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
}

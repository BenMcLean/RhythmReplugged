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

	class GameplayOptions
	{
	public:
		DifficultyOption difficulty() const;
		void set_difficulty(DifficultyOption difficulty);

	private:
		DifficultyOption difficulty_ = DifficultyOption::Medium;
	};
}

#include "core/play/GameplayOptions.h"

#include <algorithm>

namespace rhythmreplugged::core
{
	namespace
	{
		void canonicalize_instruments(std::vector<InstrumentOption> &instruments)
		{
			std::sort(instruments.begin(), instruments.end(),
				[](InstrumentOption left, InstrumentOption right)
				{
					return static_cast<int>(left) < static_cast<int>(right);
				});
			instruments.erase(std::unique(instruments.begin(), instruments.end()), instruments.end());
		}
	}

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

	const std::vector<InstrumentOption> &GameplayOptions::claimed_instruments() const
	{
		return claimed_instruments_;
	}

	void GameplayOptions::set_claimed_instruments(std::vector<InstrumentOption> instruments)
	{
		canonicalize_instruments(instruments);
		claimed_instruments_ = std::move(instruments);
	}

	bool GameplayOptions::has_claimed_instrument(InstrumentOption instrument) const
	{
		return std::find(claimed_instruments_.begin(), claimed_instruments_.end(), instrument) != claimed_instruments_.end();
	}

	bool GameplayOptions::toggle_claimed_instrument(InstrumentOption instrument)
	{
		const auto it = std::find(claimed_instruments_.begin(), claimed_instruments_.end(), instrument);
		if (it != claimed_instruments_.end())
		{
			claimed_instruments_.erase(it);
			return false;
		}

		claimed_instruments_.push_back(instrument);
		canonicalize_instruments(claimed_instruments_);
		return true;
	}

	const std::vector<InstrumentOption> &GameplayOptions::reserved_instruments() const
	{
		return reserved_instruments_;
	}

	void GameplayOptions::set_reserved_instruments(std::vector<InstrumentOption> instruments)
	{
		canonicalize_instruments(instruments);
		reserved_instruments_ = std::move(instruments);
	}
}

#pragma once

#include "core/audio/PrototypePlayer.h"
#include "core/concurrency/BackgroundWorker.h"
#include "frontend_contract/RetroFileSystem.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace rhythmreplugged::core
{
	struct SongPreloadStatus
	{
		bool active = false;
		bool ready = false;
		bool failed = false;
		size_t processed_bytes = 0;
		size_t total_bytes = 0;
		size_t completed_stem_count = 0;
		size_t total_stem_count = 0;
		std::string error_message;
	};

	class SongPreloader
	{
	public:
		explicit SongPreloader(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system);
		~SongPreloader();

		SongPreloader(const SongPreloader &) = delete;
		SongPreloader &operator=(const SongPreloader &) = delete;

		void begin(const std::string &song_directory);
		void cancel();
		SongPreloadStatus status() const;
		bool try_take_ready_data(std::string &song_directory, PrototypePlayer::PreloadedSongData &preloaded_song_data);

	private:
		struct RequestState
		{
			std::string song_directory;
			SongMetadataView metadata;
			std::vector<PrototypePlayer::PreloadedStemTrack> stems;
			std::vector<size_t> stem_order_indices;
			std::atomic<size_t> processed_bytes{0};
			std::atomic<size_t> total_bytes{0};
			std::atomic<size_t> completed_stem_count{0};
			size_t total_stem_count = 0;
			std::atomic<bool> failed{false};
			std::atomic<bool> ready{false};
			std::mutex result_mutex;
			std::string error_message;
		};

		void finalize_request(std::uint64_t generation, const std::shared_ptr<RequestState> &request_state);

		::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system_;
		BackgroundWorker worker_;
		std::atomic<std::uint64_t> generation_{0};
		mutable std::mutex state_mutex_;
		std::shared_ptr<RequestState> current_request_;
		std::optional<std::pair<std::string, PrototypePlayer::PreloadedSongData>> ready_result_;
	};
}

#pragma once

#include "core/app/AppTypes.h"
#include "core/audio/SongPlayer.h"
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
		PreloadPhase phase = PreloadPhase::Idle;
		int sample_rate = 0;
		size_t processed_bytes = 0;
		size_t total_bytes = 0;
		size_t completed_stem_count = 0;
		size_t total_stem_count = 0;
		size_t completed_read_file_count = 0;
		size_t total_read_file_count = 0;
		std::string error_message;
	};

	class SongPreloader
	{
	public:
		explicit SongPreloader(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system);
		~SongPreloader();

		SongPreloader(const SongPreloader &) = delete;
		SongPreloader &operator=(const SongPreloader &) = delete;

		void set_multithreaded_file_loading_enabled(bool enabled);
		void begin(const std::string &song_directory);
		void cancel();
		SongPreloadStatus status() const;
		bool try_take_ready_data(std::string &song_directory, SongPlayer::PreloadedSongData &preloaded_song_data);

	private:
		struct StagedStemJob
		{
			size_t order_index = 0;
			std::string stem_name;
			std::vector<std::uint8_t> bytes;
		};

		struct RequestState
		{
			std::string song_directory;
			SongMetadataView metadata;
			std::vector<std::shared_ptr<StagedStemJob>> staged_jobs;
			std::vector<SongPlayer::PreloadedStemTrack> stems;
			std::vector<size_t> stem_order_indices;
			std::atomic<size_t> processed_decode_bytes{0};
			std::atomic<size_t> total_bytes{0};
			std::atomic<size_t> completed_decode_stem_count{0};
			std::atomic<size_t> completed_read_file_count{0};
			size_t total_stem_count = 0;
			std::atomic<int> sample_rate{0};
			std::atomic<PreloadPhase> phase{PreloadPhase::Idle};
			std::atomic<bool> failed{false};
			std::atomic<bool> ready{false};
			std::mutex result_mutex;
			std::string error_message;
		};

		void finalize_request(std::uint64_t generation, const std::shared_ptr<RequestState> &request_state);

		::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system_;
		BackgroundWorker worker_;
		bool multithreaded_file_loading_enabled_ = true;
		size_t configured_thread_count_ = 0;
		std::atomic<std::uint64_t> generation_{0};
		mutable std::mutex state_mutex_;
		std::shared_ptr<RequestState> current_request_;
		std::optional<std::pair<std::string, SongPlayer::PreloadedSongData>> ready_result_;
	};
}

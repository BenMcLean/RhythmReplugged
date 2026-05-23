#include "core/play/SongPreloader.h"

#include "core/audio/StemCatalog.h"
#include "core/songs/SongIni.h"

#include <algorithm>
#include <utility>

namespace rhythmreplugged::core
{
	namespace
	{
		std::string folder_name_from_path(const std::string &path)
		{
			const size_t slash = path.find_last_of("/\\");
			return slash == std::string::npos ? path : path.substr(slash + 1);
		}
	}

	SongPreloader::SongPreloader(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system)
		: file_system_(file_system)
	{
	}

	SongPreloader::~SongPreloader()
	{
		worker_.stop();
	}

	void SongPreloader::set_multithreaded_file_loading_enabled(bool enabled)
	{
		multithreaded_file_loading_enabled_ = enabled;
	}

	void SongPreloader::begin(const std::string &song_directory)
	{
		cancel();
		const std::uint64_t generation = generation_.fetch_add(1) + 1;

		const SongIniParseResult parse_result = parse_song_ini(file_system_, song_directory + "/song.ini");
		std::string error_message;
		if (!parse_result.has_song_section || !parse_result.parsed_successfully)
		{
			std::scoped_lock lock(state_mutex_);
			current_request_.reset();
			ready_result_.reset();
			auto request_state = std::make_shared<RequestState>();
			request_state->song_directory = song_directory;
			request_state->error_message = parse_result.error_message.empty() ? "Could not parse song.ini." : parse_result.error_message;
			request_state->failed.store(true);
			request_state->phase.store(PreloadPhase::Failed);
			current_request_ = request_state;
			return;
		}

		std::vector<std::pair<size_t, std::string>> available_stems;
		available_stems.reserve(kPlayableStemNames.size());
		for (size_t order_index = 0; order_index < kPlayableStemNames.size(); ++order_index)
		{
			const std::string stem_name(kPlayableStemNames[order_index]);
			const std::string stem_path = song_directory + "/" + stem_name + ".ogg";
			if (!file_system_.path_exists(stem_path))
				continue;
			available_stems.emplace_back(order_index, stem_name);
		}

		auto request_state = std::make_shared<RequestState>();
		request_state->song_directory = song_directory;
		request_state->metadata = make_song_metadata_view(parse_result.metadata, folder_name_from_path(song_directory));
		request_state->total_stem_count = available_stems.size();
		request_state->stems.reserve(available_stems.size());
		request_state->stem_order_indices.reserve(available_stems.size());
		request_state->phase.store(PreloadPhase::Reading);

		{
			std::scoped_lock lock(state_mutex_);
			current_request_ = request_state;
			ready_result_.reset();
		}

		if (available_stems.empty())
		{
			request_state->failed.store(true);
			request_state->phase.store(PreloadPhase::Failed);
			std::scoped_lock lock(request_state->result_mutex);
			request_state->error_message = "Could not find any supported .ogg stems.";
			return;
		}

		const size_t desired_thread_count = multithreaded_file_loading_enabled_
			? BackgroundWorker::automatic_thread_count(available_stems.size())
			: 1;
		if (worker_.running() && configured_thread_count_ != desired_thread_count)
		{
			worker_.stop();
			configured_thread_count_ = 0;
		}
		if (!worker_.running())
		{
			worker_.start(desired_thread_count);
			configured_thread_count_ = desired_thread_count;
		}

		for (const auto &[order_index, stem_name] : available_stems)
		{
			const std::string stem_path = song_directory + "/" + stem_name + ".ogg";
			if (!worker_.enqueue([this, generation, request_state, order_index, stem_name, stem_path]()
			{
				if (generation_.load() != generation || request_state->failed.load())
					return;

				const auto stem_bytes = file_system_.read_binary_file(stem_path);
				if (!stem_bytes.has_value())
				{
					request_state->failed.store(true);
					request_state->phase.store(PreloadPhase::Failed);
					std::scoped_lock lock(request_state->result_mutex);
					request_state->error_message = "Could not read " + stem_name + ".ogg.";
					return;
				}

				auto staged_job = std::make_shared<StagedStemJob>();
				staged_job->order_index = order_index;
				staged_job->stem_name = stem_name;
				staged_job->bytes = std::move(*stem_bytes);
				{
					std::scoped_lock lock(request_state->result_mutex);
					request_state->total_bytes += staged_job->bytes.size();
					request_state->staged_jobs.push_back(staged_job);
				}

				const size_t completed_reads = request_state->completed_read_file_count.fetch_add(1) + 1;
				if (completed_reads == request_state->total_stem_count)
					request_state->phase.store(PreloadPhase::Decoding);

				if (!worker_.enqueue([this, generation, request_state, staged_job]()
				{
					if (generation_.load() != generation || request_state->failed.load())
						return;

					size_t last_reported_bytes = 0;
					SongPlayer::PreloadedStemTrack track;
					track.stem_name = staged_job->stem_name;
					std::string error_message;
					if (!SongPlayer::decode_preloaded_stem(staged_job->bytes, track, error_message,
						[&](size_t processed_bytes, size_t)
						{
							const size_t clamped_bytes = (std::min)(processed_bytes, staged_job->bytes.size());
							if (clamped_bytes <= last_reported_bytes)
								return;

							request_state->processed_decode_bytes.fetch_add(clamped_bytes - last_reported_bytes);
							last_reported_bytes = clamped_bytes;
						}))
					{
						request_state->failed.store(true);
						request_state->phase.store(PreloadPhase::Failed);
						std::scoped_lock lock(request_state->result_mutex);
						request_state->error_message = "Failed to decode " + staged_job->stem_name + ".ogg: " + error_message;
						return;
					}

					{
						std::scoped_lock lock(request_state->result_mutex);
						request_state->stem_order_indices.push_back(staged_job->order_index);
						request_state->stems.push_back(std::move(track));
					}
					request_state->sample_rate.store(track.sample_rate);

					const size_t completed_decode_count = request_state->completed_decode_stem_count.fetch_add(1) + 1;
					if (completed_decode_count == request_state->total_stem_count)
						finalize_request(generation, request_state);
				}))
				{
					request_state->failed.store(true);
					request_state->phase.store(PreloadPhase::Failed);
					std::scoped_lock lock(request_state->result_mutex);
					request_state->error_message = "Could not queue stem decode work.";
				}
			}, BackgroundWorker::JobPriority::High))
			{
				request_state->failed.store(true);
				request_state->phase.store(PreloadPhase::Failed);
				std::scoped_lock lock(request_state->result_mutex);
				request_state->error_message = "Could not queue stem read work.";
				return;
			}
		}
	}

	void SongPreloader::cancel()
	{
		generation_.fetch_add(1);
		worker_.clear_pending();
		std::scoped_lock lock(state_mutex_);
		current_request_.reset();
		ready_result_.reset();
	}

	SongPreloadStatus SongPreloader::status() const
	{
		SongPreloadStatus result;
		std::shared_ptr<RequestState> request_state;
		{
			std::scoped_lock lock(state_mutex_);
			request_state = current_request_;
		}

		if (request_state == nullptr)
			return result;

		result.active = !request_state->ready.load() && !request_state->failed.load();
		result.ready = request_state->ready.load();
		result.failed = request_state->failed.load();
		result.phase = request_state->phase.load();
		result.sample_rate = request_state->sample_rate.load();
		result.processed_bytes = request_state->processed_decode_bytes.load();
		result.total_bytes = request_state->total_bytes;
		result.completed_stem_count = request_state->completed_decode_stem_count.load();
		result.total_stem_count = request_state->total_stem_count;
		result.completed_read_file_count = request_state->completed_read_file_count.load();
		result.total_read_file_count = request_state->total_stem_count;
		std::scoped_lock lock(request_state->result_mutex);
		result.error_message = request_state->error_message;
		return result;
	}

	bool SongPreloader::try_take_ready_data(std::string &song_directory, SongPlayer::PreloadedSongData &preloaded_song_data)
	{
		std::scoped_lock lock(state_mutex_);
		if (!ready_result_.has_value())
			return false;

		song_directory = std::move(ready_result_->first);
		preloaded_song_data = std::move(ready_result_->second);
		ready_result_.reset();
		return true;
	}

	void SongPreloader::finalize_request(std::uint64_t generation, const std::shared_ptr<RequestState> &request_state)
	{
		if (generation_.load() != generation || request_state->failed.load())
			return;

		SongPlayer::PreloadedSongData ready_data;
		ready_data.metadata = request_state->metadata;
		{
			std::scoped_lock lock(request_state->result_mutex);
			std::vector<size_t> order(request_state->stems.size());
			for (size_t index = 0; index < order.size(); ++index)
				order[index] = index;
			std::sort(order.begin(), order.end(), [&](size_t left, size_t right)
			{
				return request_state->stem_order_indices[left] < request_state->stem_order_indices[right];
			});

			ready_data.stems.reserve(request_state->stems.size());
			for (size_t index : order)
				ready_data.stems.push_back(std::move(request_state->stems[index]));
			request_state->staged_jobs.clear();
		}

		request_state->ready.store(true);
		request_state->phase.store(PreloadPhase::Ready);
		std::scoped_lock lock(state_mutex_);
		if (generation_.load() != generation)
			return;
		ready_result_ = std::make_pair(request_state->song_directory, std::move(ready_data));
	}
}

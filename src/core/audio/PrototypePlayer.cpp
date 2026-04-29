#include "core/audio/PrototypePlayer.h"

#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rhythmreplugged::core
{
	namespace
	{
		constexpr float kStemFadeDurationSeconds = 0.012f;
		constexpr size_t kProgressReportBytes = 1024u * 1024u;

		std::string folder_name_from_path(const std::string &path)
		{
			const size_t slash = path.find_last_of("/\\");
			return slash == std::string::npos ? path : path.substr(slash + 1);
		}

		struct MemoryVorbisSource
		{
			const std::uint8_t *bytes = nullptr;
			size_t size = 0;
			size_t offset = 0;
		};

		size_t memory_read(void *ptr, size_t size, size_t nmemb, void *datasource)
		{
			auto *source = static_cast<MemoryVorbisSource *>(datasource);
			const size_t requested = size * nmemb;
			const size_t available = std::min(requested, source->size - source->offset);
			std::memcpy(ptr, source->bytes + source->offset, available);
			source->offset += available;
			return size == 0 ? 0 : available / size;
		}

		int memory_seek(void *datasource, ogg_int64_t offset, int whence)
		{
			auto *source = static_cast<MemoryVorbisSource *>(datasource);
			size_t next_offset = source->offset;
			switch (whence)
			{
			case SEEK_SET:
				next_offset = static_cast<size_t>(offset);
				break;
			case SEEK_CUR:
				next_offset = static_cast<size_t>(static_cast<ogg_int64_t>(source->offset) + offset);
				break;
			case SEEK_END:
				next_offset = static_cast<size_t>(static_cast<ogg_int64_t>(source->size) + offset);
				break;
			default:
				return -1;
			}

			if (next_offset > source->size)
				return -1;

			source->offset = next_offset;
			return 0;
		}

		long memory_tell(void *datasource)
		{
			auto *source = static_cast<MemoryVorbisSource *>(datasource);
			return static_cast<long>(source->offset);
		}

		const ov_callbacks k_memory_callbacks = {
			memory_read,
			memory_seek,
			nullptr,
			memory_tell};

		float step_towards(float current, float target, float max_step)
		{
			if (current < target)
				return std::min(current + max_step, target);

			return std::max(current - max_step, target);
		}
	}

	PrototypePlayer::StemTrack::StemTrack(StemTrack &&other) noexcept
		: stem_name(std::move(other.stem_name)),
		  samples(std::move(other.samples)),
		  channels(other.channels),
		  sample_rate(other.sample_rate),
		  frame_count(other.frame_count),
		  current_gain(other.current_gain),
		  target_gain(other.target_gain.load())
	{
	}

	PrototypePlayer::StemTrack &PrototypePlayer::StemTrack::operator=(StemTrack &&other) noexcept
	{
		if (this == &other)
			return *this;

		stem_name = std::move(other.stem_name);
		samples = std::move(other.samples);
		channels = other.channels;
		sample_rate = other.sample_rate;
		frame_count = other.frame_count;
		current_gain = other.current_gain;
		target_gain.store(other.target_gain.load());
		return *this;
	}

	bool PrototypePlayer::load(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system, const std::string &song_directory, std::string &error_message)
	{
		PreloadedSongData preloaded_song_data;
		if (!preload(file_system, song_directory, preloaded_song_data, error_message))
			return false;
		return load_preloaded(std::move(preloaded_song_data), error_message);
	}

	bool PrototypePlayer::load_preloaded(PreloadedSongData preloaded_song_data, std::string &error_message)
	{
		return adopt_preloaded(std::move(preloaded_song_data), error_message);
	}

	bool PrototypePlayer::preload(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
		const std::string &song_directory,
		PreloadedSongData &preloaded_song_data,
		std::string &error_message)
	{
		preloaded_song_data = {};

		const SongIniParseResult parse_result = parse_song_ini(file_system, song_directory + "/song.ini");
		if (!parse_result.has_song_section || !parse_result.parsed_successfully)
		{
			error_message = parse_result.error_message.empty() ? "Could not parse song.ini." : parse_result.error_message;
			return false;
		}

		preloaded_song_data.metadata = make_song_metadata_view(parse_result.metadata, folder_name_from_path(song_directory));
		preloaded_song_data.stems.clear();
		preloaded_song_data.stems.reserve(kPlayableStemNames.size());

		for (std::string_view stem_name : kPlayableStemNames)
		{
			const std::string stem_path = song_directory + "/" + std::string(stem_name) + ".ogg";
			const auto stem_bytes = file_system.read_binary_file(stem_path);
			if (!stem_bytes.has_value())
				continue;

			PreloadedStemTrack track;
			track.stem_name = stem_name;
			if (!decode_preloaded_stem(*stem_bytes, track, error_message))
			{
				error_message = "Failed to decode " + std::string(stem_name) + ".ogg: " + error_message;
				preloaded_song_data = {};
				return false;
			}

			preloaded_song_data.stems.push_back(std::move(track));
		}

		if (preloaded_song_data.stems.empty())
		{
			error_message = "Could not find any supported .ogg stems.";
			return false;
		}

		return true;
	}

	void PrototypePlayer::unload()
	{
		stems_.clear();
		frame_index_ = 0;
		metadata_ = {};
	}

	bool PrototypePlayer::is_loaded() const
	{
		return !stems_.empty();
	}

	void PrototypePlayer::toggle_guitar_mute()
	{
		set_stem_target_gain("guitar", stem_target_gain("guitar") > 0.5f ? 0.0f : 1.0f);
	}

	bool PrototypePlayer::guitar_muted() const
	{
		return has_stem("guitar") && stem_target_gain("guitar") < 0.5f;
	}

	bool PrototypePlayer::has_stem(std::string_view stem_name) const
	{
		return find_stem(stem_name) != nullptr;
	}

	size_t PrototypePlayer::loaded_stem_count() const
	{
		return stems_.size();
	}

	void PrototypePlayer::set_stem_target_gain(std::string_view stem_name, float gain)
	{
		if (StemTrack *track = find_stem(stem_name))
			track->target_gain.store(std::clamp(gain, 0.0f, 1.0f));
	}

	float PrototypePlayer::stem_target_gain(std::string_view stem_name) const
	{
		if (const StemTrack *track = find_stem(stem_name))
			return track->target_gain.load();
		return 0.0f;
	}

	int PrototypePlayer::sample_rate() const
	{
		return stems_.empty() ? 0 : stems_.front().sample_rate;
	}

	double PrototypePlayer::duration_seconds() const
	{
		const int rate = sample_rate();
		if (rate <= 0)
			return 0.0;

		return static_cast<double>(longest_track_frame_count()) / static_cast<double>(rate);
	}

	const SongMetadataView &PrototypePlayer::metadata() const
	{
		return metadata_;
	}

	bool PrototypePlayer::playback_finished() const
	{
		return !stems_.empty() && frame_index_ >= longest_track_frame_count();
	}

	void PrototypePlayer::rewind()
	{
		frame_index_ = 0;
		for (StemTrack &track : stems_)
			track.current_gain = track.target_gain.load();
	}

	void PrototypePlayer::render_interleaved_s16(std::int16_t *output, size_t frame_count)
	{
		if (output == nullptr || frame_count == 0)
			return;

		const float fade_step = sample_rate() > 0
			? 1.0f / (static_cast<float>(sample_rate()) * kStemFadeDurationSeconds)
			: 1.0f;
		const size_t longest_track_frames = longest_track_frame_count();

		for (size_t frame = 0; frame < frame_count; ++frame)
		{
			for (StemTrack &track : stems_)
				track.current_gain = step_towards(track.current_gain, track.target_gain.load(), fade_step);

			for (int channel = 0; channel < 2; ++channel)
			{
				const size_t output_index = frame * 2 + static_cast<size_t>(channel);
				float mixed = 0.0f;
				for (StemTrack &track : stems_)
				{
					mixed += sample_track_channel(track, frame_index_, channel) * track.current_gain;
				}
				mixed = std::clamp(mixed, -1.0f, 1.0f);
				output[output_index] = static_cast<std::int16_t>(std::lrintf(mixed * 32767.0f));
			}

			if (frame_index_ < longest_track_frames)
				++frame_index_;
		}
	}

	::rhythmreplugged::frontend_contract::AudioBatch PrototypePlayer::generate_audio_batch(size_t frame_count)
	{
		::rhythmreplugged::frontend_contract::AudioBatch batch;
		batch.sample_rate = sample_rate();
		batch.channels = 2;
		batch.samples.resize(frame_count * 2);
		render_interleaved_s16(batch.samples.data(), frame_count);
		return batch;
	}

	bool PrototypePlayer::decode_preloaded_stem(const std::vector<std::uint8_t> &bytes,
		PreloadedStemTrack &track,
		std::string &error_message,
		const DecodeProgressCallback &progress_callback)
	{
		return decode_vorbis(bytes, track, error_message, progress_callback);
	}

	bool PrototypePlayer::decode_vorbis(const std::vector<std::uint8_t> &bytes,
		PreloadedStemTrack &track,
		std::string &error_message,
		const DecodeProgressCallback &progress_callback)
	{
		MemoryVorbisSource source{
			.bytes = bytes.data(),
			.size = bytes.size(),
			.offset = 0};

		OggVorbis_File vorbis_file{};
		if (ov_open_callbacks(&source, &vorbis_file, nullptr, 0, k_memory_callbacks) < 0)
		{
			error_message = "ov_open_callbacks failed while decoding OGG data.";
			return false;
		}

		bool success = false;
		size_t last_reported_offset = 0;
		do
		{
			vorbis_info *info = ov_info(&vorbis_file, -1);
			if (info == nullptr)
			{
				error_message = "ov_info failed while reading OGG stream info.";
				break;
			}

			track.channels = info->channels;
			track.sample_rate = static_cast<int>(info->rate);
			if (track.channels <= 0 || track.sample_rate <= 0)
			{
				error_message = "OGG stream has an invalid channel count or sample rate.";
				break;
			}

			float **pcm_channels = nullptr;
			for (;;)
			{
				const long frames_read = ov_read_float(&vorbis_file, &pcm_channels, 4096, nullptr);
				if (frames_read == 0)
				{
					if (progress_callback)
						progress_callback(bytes.size(), bytes.size());
					success = true;
					break;
				}

				if (frames_read < 0)
				{
					error_message = "ov_read_float failed while decoding OGG samples.";
					break;
				}

				const size_t start_index = track.samples.size();
				track.samples.resize(start_index + static_cast<size_t>(frames_read) * static_cast<size_t>(track.channels));

				for (long frame = 0; frame < frames_read; ++frame)
				{
					for (int channel = 0; channel < track.channels; ++channel)
					{
						track.samples[start_index + static_cast<size_t>(frame) * static_cast<size_t>(track.channels) + static_cast<size_t>(channel)] =
							pcm_channels[channel][frame];
					}
				}

				if (progress_callback && (source.offset >= last_reported_offset + kProgressReportBytes || source.offset == bytes.size()))
				{
					last_reported_offset = source.offset;
					progress_callback(source.offset, bytes.size());
				}
			}
		}
		while (false);

		ov_clear(&vorbis_file);
		track.frame_count = track.channels > 0 ? track.samples.size() / static_cast<size_t>(track.channels) : 0;
		return success;
	}

	bool PrototypePlayer::adopt_preloaded(PreloadedSongData preloaded_song_data, std::string &error_message)
	{
		unload();
		metadata_ = std::move(preloaded_song_data.metadata);
		stems_.clear();
		stems_.reserve(preloaded_song_data.stems.size());

		int session_sample_rate = 0;
		for (PreloadedStemTrack &preloaded_track : preloaded_song_data.stems)
		{
			if (preloaded_track.channels != 1 && preloaded_track.channels != 2)
			{
				error_message = preloaded_track.stem_name + ".ogg must be mono or stereo.";
				unload();
				return false;
			}

			if (session_sample_rate == 0)
				session_sample_rate = preloaded_track.sample_rate;
			else if (preloaded_track.sample_rate != session_sample_rate)
			{
				error_message = "All loaded OGG stems must share the same sample rate.";
				unload();
				return false;
			}

			StemTrack track;
			track.stem_name = std::move(preloaded_track.stem_name);
			track.samples = std::move(preloaded_track.samples);
			track.channels = preloaded_track.channels;
			track.sample_rate = preloaded_track.sample_rate;
			track.frame_count = preloaded_track.frame_count;
			stems_.push_back(std::move(track));
		}

		if (stems_.empty())
		{
			error_message = "Could not find any supported .ogg stems.";
			return false;
		}

		frame_index_ = 0;
		return true;
	}

	PrototypePlayer::StemTrack *PrototypePlayer::find_stem(std::string_view stem_name)
	{
		for (StemTrack &track : stems_)
		{
			if (track.stem_name == stem_name)
				return &track;
		}

		return nullptr;
	}

	const PrototypePlayer::StemTrack *PrototypePlayer::find_stem(std::string_view stem_name) const
	{
		for (const StemTrack &track : stems_)
		{
			if (track.stem_name == stem_name)
				return &track;
		}

		return nullptr;
	}

	float PrototypePlayer::sample_track_channel(const StemTrack &track, size_t frame_index, int channel) const
	{
		if (frame_index >= track.frame_count)
			return 0.0f;

		const int resolved_channel = track.channels == 1 ? 0 : channel;
		if (resolved_channel >= track.channels)
			return 0.0f;

		return track.samples[frame_index * static_cast<size_t>(track.channels) + static_cast<size_t>(resolved_channel)];
	}

	size_t PrototypePlayer::longest_track_frame_count() const
	{
		size_t longest = 0;
		for (const StemTrack &track : stems_)
			longest = std::max(longest, track.frame_count);

		return longest;
	}
}

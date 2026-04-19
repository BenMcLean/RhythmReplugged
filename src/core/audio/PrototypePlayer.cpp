#include "core/audio/PrototypePlayer.h"

#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rhythmreplugged
{
	namespace
	{
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
	}

	bool PrototypePlayer::load(IRetroFileSystem &file_system, const std::string &song_directory, std::string &error_message)
	{
		unload();

		const SongIniParseResult parse_result = parse_song_ini(file_system, song_directory + "/song.ini");
		if (!parse_result.has_song_section || !parse_result.parsed_successfully)
		{
			error_message = parse_result.error_message.empty() ? "Could not parse song.ini." : parse_result.error_message;
			return false;
		}

		metadata_ = make_song_metadata_view(parse_result.metadata, folder_name_from_path(song_directory));

		const auto backing_bytes = file_system.read_binary_file(song_directory + "/song.ogg");
		if (!backing_bytes.has_value())
		{
			error_message = "Could not read song.ogg.";
			return false;
		}

		const auto guitar_bytes = file_system.read_binary_file(song_directory + "/guitar.ogg");
		if (!guitar_bytes.has_value())
		{
			error_message = "Could not read guitar.ogg.";
			return false;
		}

		if (!decode_vorbis(*backing_bytes, backing_track_, error_message))
			return false;

		if (!decode_vorbis(*guitar_bytes, guitar_track_, error_message))
			return false;

		if (backing_track_.channels != 2 || guitar_track_.channels != 2)
		{
			error_message = "Prototype playback currently requires stereo song.ogg and guitar.ogg.";
			unload();
			return false;
		}

		if (backing_track_.sample_rate != guitar_track_.sample_rate)
		{
			error_message = "song.ogg and guitar.ogg do not share the same sample rate.";
			unload();
			return false;
		}

		frame_index_ = 0;
		guitar_muted_ = false;
		return true;
	}

	void PrototypePlayer::unload()
	{
		backing_track_ = {};
		guitar_track_ = {};
		frame_index_ = 0;
		guitar_muted_ = false;
		metadata_ = {};
	}

	bool PrototypePlayer::is_loaded() const
	{
		return backing_track_.frame_count > 0 && guitar_track_.frame_count > 0;
	}

	void PrototypePlayer::toggle_guitar_mute()
	{
		guitar_muted_ = !guitar_muted_;
	}

	bool PrototypePlayer::guitar_muted() const
	{
		return guitar_muted_;
	}

	int PrototypePlayer::sample_rate() const
	{
		return backing_track_.sample_rate;
	}

	const SongMetadataView &PrototypePlayer::metadata() const
	{
		return metadata_;
	}

	RetroAudioBatch PrototypePlayer::generate_audio_batch(size_t frame_count)
	{
		RetroAudioBatch batch;
		batch.sample_rate = backing_track_.sample_rate;
		batch.channels = 2;
		batch.samples.resize(frame_count * 2);

		for (size_t frame = 0; frame < frame_count; ++frame)
		{
			for (int channel = 0; channel < 2; ++channel)
			{
				float mixed = sample_track_channel(backing_track_, frame_index_, channel);
				if (!guitar_muted_)
					mixed += sample_track_channel(guitar_track_, frame_index_, channel);

				mixed = std::clamp(mixed, -1.0f, 1.0f);
				batch.samples[frame * 2 + static_cast<size_t>(channel)] =
					static_cast<std::int16_t>(std::lrintf(mixed * 32767.0f));
			}

			if (frame_index_ < std::max(backing_track_.frame_count, guitar_track_.frame_count))
				++frame_index_;
		}

		return batch;
	}

	bool PrototypePlayer::decode_vorbis(const std::vector<std::uint8_t> &bytes, DecodedTrack &track, std::string &error_message)
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
			}
		}
		while (false);

		ov_clear(&vorbis_file);
		track.frame_count = track.channels > 0 ? track.samples.size() / static_cast<size_t>(track.channels) : 0;
		return success;
	}

	float PrototypePlayer::sample_track_channel(const DecodedTrack &track, size_t frame_index, int channel) const
	{
		if (frame_index >= track.frame_count || channel >= track.channels)
			return 0.0f;

		return track.samples[frame_index * static_cast<size_t>(track.channels) + static_cast<size_t>(channel)];
	}
}

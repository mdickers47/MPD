/*
 * Copyright 2003-2020 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/** \file
 *
 * Playlist plugin that reads embedded cue sheets from the "CUESHEET"
 * tag of a music file.
 */

#include "FlacPlaylistPlugin.hxx"
#include "../PlaylistPlugin.hxx"
#include "../MemorySongEnumerator.hxx"
#include "lib/xiph/FlacMetadataChain.hxx"
#include "lib/xiph/FlacMetadataIterator.hxx"
#include "song/DetachedSong.hxx"
#include "input/InputStream.hxx"
#include "util/RuntimeError.hxx"
#include "fs/Traits.hxx"
#include "fs/AllocatedPath.hxx"
#include "fs/NarrowPath.hxx"
#include "util/ScopeExit.hxx"
#include "tag/Builder.hxx"
#include "Log.hxx"
#include "decoder/plugins/FlacDomain.hxx"

#include <FLAC/metadata.h>

static char* get_vorbis_comment
(const FLAC__StreamMetadata_VorbisComment &t, const char *tagname)
{
  unsigned taglen = strlen(tagname);
  for (unsigned i = 0; i < t.num_comments; ++i) {
    if (memcmp(t.comments[i].entry, tagname, taglen) == 0 &&
        t.comments[i].entry[taglen] == '=') {
      return (char *)&(t.comments[i].entry[taglen + 1]);
    }
  }
  return nullptr;
}

static auto
ToSongEnumerator(const char *uri,
		 const FLAC__StreamMetadata_CueSheet &c,
                 const FLAC__StreamMetadata_VorbisComment &t,
		 const unsigned sample_rate,
		 const FLAC__uint64 total_samples) noexcept
{
	std::forward_list<DetachedSong> songs;
	auto tail = songs.before_begin();

	for (unsigned i = 0; i < c.num_tracks; ++i) {
		const auto &track = c.tracks[i];
		if (track.type != 0)
			continue;

		const FLAC__uint64 start = track.offset;
		const FLAC__uint64 end = i + 1 < c.num_tracks
			? c.tracks[i + 1].offset
			: total_samples;

		tail = songs.emplace_after(tail, uri);
		auto &song = *tail;
		song.SetStartTime(SongTime::FromScale(start, sample_rate));
		song.SetEndTime(SongTime::FromScale(end, sample_rate));

                /* we are going to set TAG_TRACK and TAG_TITLE right now,
                   but the rest of the metadata will be copied from the
                   shared comments in flac_container_scan. */
                TagBuilder tb(song.WritableTag());
                char tagbuf[32];
                snprintf(tagbuf, sizeof(tagbuf), "%u", i+1);
                tb.AddItem(TAG_TRACK, tagbuf);
                snprintf(tagbuf, sizeof(tagbuf), "TITLE[%u]", i+1);
                auto tt = get_vorbis_comment(t, tagbuf);
                FormatDebug(flac_domain, "comment %s is %s", tagbuf, tt);
                if (tt != nullptr) tb.AddItem(TAG_TITLE, tt);
                song.SetTag(tb.Commit());
	}

        /* drop any "songs" of zero length, which can happen because of
           various cuesheet and parser artifacts */
        songs.remove_if([](const DetachedSong &s){
          return s.GetStartTime() == s.GetEndTime(); }
          );

	return std::make_unique<MemorySongEnumerator>(std::move(songs));
}

/*
static std::unique_ptr<SongEnumerator>
flac_playlist_open_stream(InputStreamPtr &&is)
{
	FlacMetadataChain chain;
	if (!chain.Read(*is))
		throw FormatRuntimeError("Failed to read FLAC metadata: %s",
					 chain.GetStatusString());

	FlacMetadataIterator iterator((FLAC__Metadata_Chain *)chain);

	unsigned sample_rate = 0;
	FLAC__uint64 total_samples;

	do {
		auto &block = *iterator.GetBlock();
		switch (block.type) {
		case FLAC__METADATA_TYPE_STREAMINFO:
			sample_rate = block.data.stream_info.sample_rate;
			total_samples = block.data.stream_info.total_samples;
			break;

		case FLAC__METADATA_TYPE_CUESHEET:
			if (sample_rate == 0)
				break;

			return ToSongEnumerator("", block.data.cue_sheet,
						sample_rate, total_samples);

		default:
			break;
		}
	} while (iterator.Next());

	return nullptr;
}
*/

static std::unique_ptr<SongEnumerator>
flac_playlist_open_uri(const char *uri, [[maybe_unused]] Mutex &mutex)
{
  if (!PathTraitsUTF8::IsAbsolute(uri))
    /* only local files supported */
    return nullptr;

  const auto path_fs = AllocatedPath::FromUTF8Throw(uri);
  const NarrowPath narrow_path_fs(path_fs);
  FLAC__StreamMetadata *cuesheet;
  if (!FLAC__metadata_get_cuesheet(narrow_path_fs, &cuesheet))
    return nullptr;
  FLAC__StreamMetadata *tags;
  FLAC__metadata_get_tags(narrow_path_fs, &tags);
  AtScopeExit(cuesheet) { FLAC__metadata_object_delete(cuesheet); };
  AtScopeExit(tags) { FLAC__metadata_object_delete(tags); };

  FLAC__StreamMetadata streaminfo;
  if (!FLAC__metadata_get_streaminfo(narrow_path_fs, &streaminfo) ||
      streaminfo.data.stream_info.sample_rate == 0) {
    return nullptr;
  }

  const unsigned sample_rate = streaminfo.data.stream_info.sample_rate;
  const FLAC__uint64 total_samples = streaminfo.data.stream_info.total_samples;

  return ToSongEnumerator(uri, cuesheet->data.cue_sheet,
                          tags->data.vorbis_comment,
                          sample_rate, total_samples);
}

static const char *const flac_playlist_suffixes[] = {
	"flac",
	nullptr
};

const PlaylistPlugin flac_playlist_plugin =
	PlaylistPlugin("flac", flac_playlist_open_uri)
	.WithSuffixes(flac_playlist_suffixes);

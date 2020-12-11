# mikey's fork of Music Player Daemon

This fork has been modified so that it understands a specific species of
.flac file:

- The cuesheet is embedded in a FLAC-native cuesheet metadata block.
  (NOT in a vorbis comment.  NOT in a separate .cue file on the disk.)
- The "tags" aka vorbis_comment metadata block contains an array of TITLE
  tags, which are the titles of the individual tracks.
  (Example: 'TITLE[4]=Round Midnight')

If you have this flac variant, then the individual tracks will be parsed
out and indexed correctly in the database.

Internet reading seems to show that for over 11 years, this feature
has been requested periodically, and people have patched it in, and
then the fix is lost or forgotten in the confusion as people talk past
each other, failing to understand that they are all looking at subtly
different FLAC formats.

The implementation is hacky and probably breaks features that are
important to other people.  It rewrites the FLAC "playlist plugin"
to work on URIs instead of input streams, which definitely kills the
ability to parse cuesheets out of streamed FLAC.  (I don't understand
the point of this, since you would have to have a seekable stream for
this to make any sense.)  It was based on a patch posted by martinKempa
in 2018 (link below).  It required substantial forward-porting since
much of the internal MPD API has mutated from C to C++ in the meantime.

References:

- [A 2009 mailing list thread](https://mpd-devel.musicpd.narkive.com/xgFO5bGx/flac-embedded-cuesheet-support)
- [The 2018 patch written by martinKempa](https://github.com/MusicPlayerDaemon/MPD/issues/39)
- [Someone on reddit asking for this function in 2019](https://www.reddit.com/r/linuxquestions/comments/dy9xhj/mpd_with_flacs_that_have_embedded_cue_sheets/)

Upstream MPD README.md follows.

# Music Player Daemon

http://www.musicpd.org

A daemon for playing music of various formats.  Music is played through the 
server's audio device.  The daemon stores info about all available music, 
and this info can be easily searched and retrieved.  Player control, info
retrieval, and playlist management can all be managed remotely.

For basic installation instructions
[read the manual](https://www.musicpd.org/doc/user/install.html).

# Users

- [Manual](http://www.musicpd.org/doc/user/)
- [Forum](http://forum.musicpd.org/)
- [IRC](irc://chat.freenode.net/#mpd)
- [Bug tracker](https://github.com/MusicPlayerDaemon/MPD/issues/)

# Developers

- [Protocol specification](http://www.musicpd.org/doc/protocol/)
- [Developer manual](http://www.musicpd.org/doc/developer/)

# Legal

MPD is released under the
[GNU General Public License version 2](https://www.gnu.org/licenses/gpl-2.0.txt),
which is distributed in the COPYING file.

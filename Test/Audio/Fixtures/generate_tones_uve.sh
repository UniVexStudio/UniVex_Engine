#!/usr/bin/env bash
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
# Self-authored mathematical test signals, NOT third-party recordings.
# Reproduce with FFmpeg 7.0.2. FFmpeg is a fixture-generation tool only; no FFmpeg
# binary or code is redistributed or linked into the engine. The generated data
# contains only this mathematical signal; no recording or composition was copied.
# The fixtures follow the repository's existing copyright policy.
set -eu
cd "$(dirname "$0")"
ffmpeg=${FFMPEG:-ffmpeg}
for format in flac mp3 ogg; do
    codec=flac
    extra=()
    if [ "$format" = mp3 ]; then codec=libmp3lame; extra=(-b:a 32k); fi
    if [ "$format" = ogg ]; then codec=libvorbis; fi
    "$ffmpeg" -hide_banner -loglevel error -f lavfi \
        -i 'sine=frequency=440:sample_rate=8000:duration=0.064' -ac 1 \
        -map_metadata -1 -fflags +bitexact -flags:a +bitexact \
        -c:a "$codec" "${extra[@]}" -y "tone_uve.$format"
done
sha256sum tone_uve.flac tone_uve.mp3 tone_uve.ogg

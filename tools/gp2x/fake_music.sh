#!/bin/bash
# Populate Payback's Data/Music with valid .ama files (copies of a Speech .ama) so
# the music worker can AMA_Open them instead of error-looping on the missing files.
SRC=/home/zachd/pbtest/Data/Speech/01-27.wav.ama
cd /home/zachd/pbtest/Data/Music || exit 1
while IFS= read -r line; do
  name=$(printf '%s' "$line" | tr -d '\r\n')
  [ -n "$name" ] && [ "$name" != "_Playlist.txt" ] && cp -f "$SRC" "./$name"
done < _Playlist.txt
echo "music files now: $(ls -1 | wc -l)"
ls -1 | head -4

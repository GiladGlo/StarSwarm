# Fingerprint of the code that generates the game's art and sound (run at build time).
# The startup cache stores this value; when any of these files change, the game rebuilds its cache once.
# Usage: cmake -DFILES=a,b,c -DOUT=contenthash.h -P contenthash.cmake
string(REPLACE "," ";" files "${FILES}")
set(content "")
foreach(f IN LISTS files)
  file(READ "${f}" c)
  string(APPEND content "${c}")
endforeach()
string(SHA1 h "${content}")
set(text "#pragma once\n#define CONTENT_HASH \"${h}\"\n")
set(old "")
if(EXISTS "${OUT}")
  file(READ "${OUT}" old)
endif()
if(NOT "${old}" STREQUAL "${text}")
  file(WRITE "${OUT}" "${text}")   # only touch it when the hash changes, so unrelated edits don't trigger rebuilds
endif()

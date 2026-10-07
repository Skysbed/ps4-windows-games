#ifndef PCPS4_VERSION_H
#define PCPS4_VERSION_H

/*
 * Bump this label for every diagnostic PKG release. Runtime, game, trace, and
 * crash logs incorporate this value so files from different installed builds
 * do not overwrite each other.
 */
#define PCPS4_BUILD_VERSION "v8"
#define PCPS4_RUNTIME_LOG_PATH "/data/pcps4_" PCPS4_BUILD_VERSION "_runtime_log.txt"
#define PCPS4_WINDOWS_GAME_LOG_PATH "/data/pcps4_" PCPS4_BUILD_VERSION "_windows_game_log.txt"
#define PCPS4_TRACE_PATH "/data/pcps4_" PCPS4_BUILD_VERSION "_trace.txt"
#define PCPS4_CRASH_PATH "/data/pcps4_" PCPS4_BUILD_VERSION "_crash.txt"

#endif

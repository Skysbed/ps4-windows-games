/* Mac test harness: run a Windows game through the real PcPs4 runtime. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pcps4_game.h"
#include "pcps4_status.h"

int main(int argc, char **argv) {
    int32_t ret = -1;
    const char *game = argc > 1 ? argv[1] : "/app0/game/game.exe";
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[host] running %s (root=%s)\n", game, getenv("PCPS4_HOST_ROOT") ? getenv("PCPS4_HOST_ROOT") : "/tmp/pcps4_host");
    int rc = pcps4_game_run_file(game, &ret);
    printf("[host] pcps4_game_run_file returned %d, entry returned %d\n", rc, (int)ret);
    return rc == 0 ? 0 : 1;
}

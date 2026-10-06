#include <stdio.h>
#include "pcps4_compat.h"

int main(void)
{
    int sample = pcps4_clamp_int(42, 0, 100);

    printf("PcPs4 / OpenOrbis build is running.\n");
    printf("Sample compatibility value: %d\n", sample);
    printf("Target: PS4 x86-64\n");

    return 0;
}

#include "pcps4_dxvk_diagnostics.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static FILE *make_log(const char *text)
{
    FILE *f = tmpfile();
    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    rewind(f);
    return f;
}

static void test_visible_changing_scanout_passes(void)
{
    static const char log[] =
        "[vk] QueuePresent: flip done\n"
        "[vk] QueuePresent: scanout img=0 px[0]=00000000 px[1]=00000000 "
        "px[64]=00000000 px[mid]=ff47200a\n"
        "[vk] QueuePresent: flip done\n"
        "[vk] QueuePresent: scanout img=1 px[0]=00000000 px[1]=00000000 "
        "px[64]=00000000 px[mid]=ff54301b\n";
    pcps4_dxvk_scanout_report report;
    FILE *f = make_log(log);

    assert(pcps4_dxvk_scanout_parse(f, &report) == 0);
    assert(report.flip_count == 2);
    assert(report.scanout_samples == 2);
    assert(report.nonblack_samples == 2);
    assert(report.color_changes == 1);
    assert(report.first_rgb == 0x0047200Au);
    assert(report.last_rgb == 0x0054301Bu);
    assert(pcps4_dxvk_scanout_passes_visibility_check(&report));
    fclose(f);
}

static void test_alpha_only_black_does_not_pass(void)
{
    static const char log[] =
        "QueuePresent: flip done\n"
        "QueuePresent: scanout img=0 px[0]=ff000000 px[1]=ff000000 "
        "px[64]=ff000000 px[mid]=ff000000\n"
        "QueuePresent: flip done\n"
        "QueuePresent: scanout img=1 px[0]=ff000000 px[1]=ff000000 "
        "px[64]=ff000000 px[mid]=ff000000\n";
    pcps4_dxvk_scanout_report report;
    FILE *f = make_log(log);

    assert(pcps4_dxvk_scanout_parse(f, &report) == 0);
    assert(report.flip_count == 2);
    assert(report.scanout_samples == 2);
    assert(report.nonblack_samples == 0);
    assert(report.color_changes == 0);
    assert(!pcps4_dxvk_scanout_passes_visibility_check(&report));
    fclose(f);
}

static void test_missing_samples_does_not_pass(void)
{
    static const char log[] =
        "QueuePresent: flip done\n"
        "QueuePresent: flip done\n"
        "QueuePresent: scanout img=0 px[mid]=ff123456\n";
    pcps4_dxvk_scanout_report report;
    FILE *f = make_log(log);

    assert(pcps4_dxvk_scanout_parse(f, &report) == 0);
    assert(report.flip_count == 2);
    assert(report.scanout_samples == 1);
    assert(!pcps4_dxvk_scanout_passes_visibility_check(&report));
    fclose(f);
}

int main(void)
{
    test_visible_changing_scanout_passes();
    test_alpha_only_black_does_not_pass();
    test_missing_samples_does_not_pass();
    puts("DXVK scanout diagnostics tests passed");
    return 0;
}

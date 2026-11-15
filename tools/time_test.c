/* Is QueryPerformanceCounter monotonic across threads and cores on the PS4? */
#include <windows.h>
#include <stdio.h>

static volatile LONG64 global_max;
static LONG64 back_same[8], back_cross[8], worst_cross[8], worst_same[8], min_step[8], reads[8];

static DWORD WINAPI worker(void *arg)
{
    int id = (int)(INT_PTR)arg;
    LARGE_INTEGER t;
    LONG64 last = 0, step;
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << (id % 6));
    min_step[id] = 0x7fffffffffffffffLL;
    for (int i = 0; i < 3000000; i++)
    {
        LONG64 g = global_max, cur;
        QueryPerformanceCounter(&t);
        cur = t.QuadPart;
        if (last && cur < last) { back_same[id]++; if (last - cur > worst_same[id]) worst_same[id] = last - cur; }
        if (cur < g) { back_cross[id]++; if (g - cur > worst_cross[id]) worst_cross[id] = g - cur; }
        step = cur - last;
        if (last && step > 0 && step < min_step[id]) min_step[id] = step;
        last = cur;
        for (;;)
        {
            LONG64 m = global_max;
            if (cur <= m || InterlockedCompareExchange64(&global_max, cur, m) == m) break;
        }
        reads[id]++;
        if ((i & 1023) == 0) Sleep(0);   /* let the scheduler move us */
    }
    return 0;
}

int main(void)
{
    FILE *out = fopen("Z:\\data\\stkuser\\timetest.log", "w");
    LARGE_INTEGER f, a, b;
    HANDLE th[6];
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    Sleep(1000);
    QueryPerformanceCounter(&b);
    fprintf(out, "QPF %lld Hz; Sleep(1000) measured %.3f ms\n", f.QuadPart, (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart);
    fflush(out);
    for (int i = 0; i < 6; i++) th[i] = CreateThread(NULL, 0, worker, (void *)(INT_PTR)i, 0, NULL);
    WaitForMultipleObjects(6, th, TRUE, 120000);
    for (int i = 0; i < 6; i++)
        fprintf(out, "thread %d: %lld reads, backwards same-thread %lld (worst %.3f ms), behind other threads %lld (worst %.3f ms), finest step %.3f us\n",
                i, reads[i], back_same[i], worst_same[i] * 1000.0 / f.QuadPart, back_cross[i],
                worst_cross[i] * 1000.0 / f.QuadPart, min_step[i] * 1e6 / f.QuadPart);
    {
        /* how long do short sleeps and timed waits really take? */
        const char *names[3] = {"Sleep(1)", "Sleep(5)", "Wait(event, 5 ms)"};
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        for (int k = 0; k < 3; k++)
        {
            double sum = 0, worst = 0, best = 1e9; int sleep_hist[6] = {0};
            for (int i = 0; i < 200; i++)
            {
                LARGE_INTEGER s0, s1; double ms;
                QueryPerformanceCounter(&s0);
                if (k == 0) Sleep(1); else if (k == 1) Sleep(5); else WaitForSingleObject(ev, 5);
                QueryPerformanceCounter(&s1);
                ms = (s1.QuadPart - s0.QuadPart) * 1000.0 / f.QuadPart;
                sum += ms; if (ms > worst) worst = ms; if (ms < best) best = ms;
                sleep_hist[ms < 2 ? 0 : ms < 6 ? 1 : ms < 11 ? 2 : ms < 20 ? 3 : ms < 50 ? 4 : 5]++;
            }
            fprintf(out, "%-18s avg %.2f ms, best %.2f, worst %.2f; <2ms %d, 2-6 %d, 6-11 %d, 11-20 %d, 20-50 %d, >50 %d\n",
                    names[k], sum / 200, best, worst, sleep_hist[0], sleep_hist[1], sleep_hist[2], sleep_hist[3], sleep_hist[4], sleep_hist[5]);
            fflush(out);
        }
    }
    {   /* wait_detail: what do timed waits return? */
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL), ev2 = CreateEventA(NULL, TRUE, FALSE, NULL);
        HANDLE sem = CreateSemaphoreA(NULL, 0, 1, NULL), mtx = CreateMutexA(NULL, FALSE, NULL);
        HANDLE objs[4] = {ev, ev2, sem, mtx};
        const char *nm[4] = {"auto event", "manual event", "semaphore", "mutex(owned by us)"};
        fprintf(out, "handles: %p %p %p %p (err %lu)\n", ev, ev2, sem, mtx, GetLastError());
        WaitForSingleObject(mtx, 0);
        for (int k = 0; k < 3; k++)
        {
            for (int i = 0; i < 3; i++)
            {
                LARGE_INTEGER s0, s1; DWORD r, e;
                SetLastError(0);
                QueryPerformanceCounter(&s0);
                r = WaitForSingleObject(objs[k], 20);
                e = GetLastError();
                QueryPerformanceCounter(&s1);
                fprintf(out, "Wait(%s, 20 ms) -> 0x%lx (err %lu) after %.3f ms\n", nm[k], r, e,
                        (s1.QuadPart - s0.QuadPart) * 1000.0 / f.QuadPart);
            }
        }
        {
            LARGE_INTEGER s0, s1; DWORD r;
            QueryPerformanceCounter(&s0);
            r = WaitForMultipleObjects(3, objs, FALSE, 20);
            QueryPerformanceCounter(&s1);
            fprintf(out, "WaitForMultiple(any of 3, 20 ms) -> 0x%lx after %.3f ms\n", r, (s1.QuadPart - s0.QuadPart) * 1000.0 / f.QuadPart);
        }
        fflush(out);
    }
    fprintf(out, "done\n");
    fclose(out);
    return 0;
}

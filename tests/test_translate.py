#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from pcps4_translate import find_blockers, translate


class TranslationTests(unittest.TestCase):
    def test_supported_windows_surface(self):
        source = """
#include <windows.h>
const char *text = "Sleep(GetTickCount64())";
DWORD started = GetTickCount64();
Sleep(1);
BOOL ok = TRUE;
InterlockedIncrement(&counter);
HANDLE file = CreateFileA("x", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
CloseHandle(file);
"""
        translated, report, status = translate(source, strict=True)

        self.assertEqual(status, 0)
        self.assertIn('#include "pcps4_compat.h"', translated)
        self.assertIn('uint32_t started = pcps4_monotonic_ms();', translated)
        self.assertIn('pcps4_sleep_ms(1);', translated)
        self.assertIn('int ok = 1;', translated)
        self.assertIn('pcps4_interlocked_increment(&counter);', translated)
        self.assertIn('pcps4_CreateFileA("x"', translated)
        self.assertIn('pcps4_CloseHandle(file);', translated)
        self.assertEqual(report["blockers"], [])

    def test_socket_header_and_common_calls(self):
        source = """
#include <winsock2.h>
#include <ws2tcpip.h>
WSADATA data;
WSAStartup(0x0202, &data);
WSACleanup();
closesocket(sock);
"""
        translated, report, status = translate(source, strict=True)
        self.assertEqual(status, 0)
        self.assertIn('#include "pcps4_compat.h"', translated)
        self.assertIn('pcps4_WSAStartup', translated)
        self.assertIn('pcps4_WSACleanup', translated)
        self.assertIn('pcps4_closesocket', translated)
        self.assertEqual(report["blockers"], [])

    def test_virtual_memory_and_thread_surface(self):
        source = """
#include <windows.h>
DWORD WINAPI worker(LPVOID arg) { return 0; }
HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
WaitForSingleObject(t, INFINITE);
CloseHandle(t);
void *p = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
VirtualFree(p, 4096, MEM_RELEASE);
"""
        translated, report, status = translate(source, strict=True)
        self.assertEqual(status, 0)
        self.assertIn('pcps4_CreateThread', translated)
        self.assertIn('pcps4_WaitForSingleObject', translated)
        self.assertIn('pcps4_VirtualAlloc', translated)
        self.assertIn('pcps4_VirtualFree', translated)
        self.assertEqual(report["blockers"], [])

    def test_extended_event_and_socket_surface(self):
        source = """
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
HANDLE event = CreateEventA(NULL, TRUE, FALSE, "evt");
SetEvent(event);
ResetEvent(event);
WaitForSingleObject(event, 10);
SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
connect(s, NULL, 0);
send(s, "x", 1, 0);
recv(s, buffer, 1, 0);
shutdown(s, 2);
closesocket(s);
"""
        translated, report, status = translate(source, strict=True)
        self.assertEqual(status, 0)
        self.assertIn('pcps4_CreateEventA', translated)
        self.assertIn('pcps4_SetEvent', translated)
        self.assertIn('pcps4_ResetEvent', translated)
        self.assertIn('pcps4_socket', translated)
        self.assertIn('pcps4_connect', translated)
        self.assertIn('pcps4_send', translated)
        self.assertIn('pcps4_recv', translated)
        self.assertIn('pcps4_shutdown', translated)
        self.assertEqual(report["blockers"], [])

    def test_d3d11_header_is_translated(self):
        source = """
#include <d3d11.h>
#include <dxgi.h>
HRESULT hr = D3D11CreateDeviceAndSwapChain(
    NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, 7,
    NULL, NULL, NULL, NULL, NULL);
"""
        translated, report, status = translate(source, strict=True)
        self.assertEqual(status, 0)
        self.assertIn('#include "pcps4_d3d11.h"', translated)
        self.assertEqual(report["blockers"], [])

    def test_xinput_surface(self):
        source = """
#include <windows.h>
#include <xinput.h>
XINPUT_STATE state;
DWORD rc = XInputGetState(0, &state);
XInputEnable(TRUE);
XINPUT_VIBRATION vibration = {0, 0};
XInputSetState(0, &vibration);
XINPUT_CAPABILITIES caps;
XInputGetCapabilities(0, 0, &caps);
"""
        translated, report, status = translate(source, strict=True)
        self.assertEqual(status, 0)
        self.assertIn('#include "pcps4_xinput.h"', translated)
        self.assertIn('pcps4_XInputGetState', translated)
        self.assertIn('pcps4_XInputSetState', translated)
        self.assertIn('pcps4_XInputGetCapabilities', translated)
        self.assertIn('pcps4_XInputEnable', translated)
        self.assertEqual(report["blockers"], [])

    def test_unsupported_api_is_reported(self):
        source = """
#include <d3d12.h>
HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, NULL);
"""
        blockers = find_blockers(source)
        names = {item.name for item in blockers}
        self.assertIn("DirectX 12 API", names)


if __name__ == "__main__":
    unittest.main()

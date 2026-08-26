// fb516e — THE RAW PARK WINDOW + WM_DESTROY RESCUE (Windows only).
//
// Measured truth that killed every JUCE-window park design (see WINDOWS-HANDOFF.md fb516):
//   - JUCE TopLevelWindows DESTROY their HWND on setVisible(false).
//   - Every JUCE notification about an editor peer's death arrives AFTER the HWND is gone
//     (park log: IsWindow(old)=0 at each attempt), and WebView2's controller dies with its
//     parent HWND, unrecoverably (put_ParentWindow = ERROR_INVALID_STATE ever after).
//
// So: the park target is a plain Win32 hidden window that no JUCE semantics can kill, and the
// rescue moment is the host peer's WM_DESTROY — a parent's WM_DESTROY runs BEFORE its children
// are destroyed, so the controller can still be moved out (the classic child-rescue pattern).
// This TU exists so windows.h/commctrl.h never enter the 13k-line PluginEditor.cpp (their
// min/max/GetObject macros are hostile there — the fb515 law).

#ifdef _WIN32

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#pragma comment (lib, "comctl32.lib")

extern "C" IMAGE_DOS_HEADER __ImageBase;   // this module's HINSTANCE without any JUCE dependency

namespace
{
    using TiRescueFn = void (*) (void*);

    LRESULT CALLBACK tiPeerRescueProc (HWND h, UINT msg, WPARAM w, LPARAM l,
                                       UINT_PTR, DWORD_PTR refData)
    {
        if (msg == WM_DESTROY)
        {
            // refData points at a small heap context {fn, ctx}; fire once, before children die.
            if (auto* pair = reinterpret_cast<void**> (refData))
                if (auto fn = reinterpret_cast<TiRescueFn> (pair[0]))
                    fn (pair[1]);
        }
        return DefSubclassProc (h, msg, w, l);
    }
    constexpr UINT_PTR kTiRescueId = 0x7E11;
}

void* tiCreateRawParkWindow()
{
    static bool classDone = false;
    auto* hInst = reinterpret_cast<HINSTANCE> (&__ImageBase);
    if (! classDone)
    {
        WNDCLASSEXW wc {};
        wc.cbSize        = sizeof (wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"TerrainUiParkWindow";
        RegisterClassExW (&wc);   // idempotent-enough: second call fails harmlessly
        classDone = true;
    }
    // Hidden (never shown): WebView2 accepts it as a parent, put_IsVisible(false) keeps the
    // renderer suspended, and nothing in JUCE knows this HWND exists.
    return CreateWindowExW (0, L"TerrainUiParkWindow", L"terrain-ui-park", WS_OVERLAPPED,
                            -4000, -4000, 840, 700, nullptr, nullptr, hInst, nullptr);
}

void tiDestroyRawParkWindow (void* hwnd)
{
    if (hwnd != nullptr && IsWindow ((HWND) hwnd))
        DestroyWindow ((HWND) hwnd);
}

// Arms a WM_DESTROY rescue on the host peer. Returns an opaque token (the heap context) or
// nullptr. The callback fires ON the peer's WM_DESTROY, synchronously, children still alive.
void* tiArmPeerRescue (void* peerHwnd, void (*fn) (void*), void* ctx)
{
    if (peerHwnd == nullptr || ! IsWindow ((HWND) peerHwnd))
        return nullptr;
    auto** pair = new void*[2];
    pair[0] = reinterpret_cast<void*> (fn);
    pair[1] = ctx;
    if (! SetWindowSubclass ((HWND) peerHwnd, tiPeerRescueProc, kTiRescueId, (DWORD_PTR) pair))
    {
        delete[] pair;
        return nullptr;
    }
    return pair;
}

void tiDisarmPeerRescue (void* peerHwnd, void* token)
{
    if (peerHwnd != nullptr && IsWindow ((HWND) peerHwnd))
        RemoveWindowSubclass ((HWND) peerHwnd, tiPeerRescueProc, kTiRescueId);
    delete[] reinterpret_cast<void**> (token);
}

#endif // _WIN32

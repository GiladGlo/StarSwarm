// Controller rumble through XInput (Windows, Xbox-compatible controllers).
// raylib's desktop (GLFW) backend has no vibration support, so the DLL is loaded directly at first use.
// windows.h isn't included (its names clash with raylib); the two kernel32 functions are declared by hand.
#pragma once
#include <algorithm>

#ifdef _WIN32
extern "C" {
__declspec(dllimport) void* __stdcall LoadLibraryA(const char* name);
__declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* proc);
}
namespace rumble {
struct XInputVibration { unsigned short left, right; };
typedef unsigned long(__stdcall* SetStateFn)(unsigned long userIndex, XInputVibration* v);
inline SetStateFn setState() {
    static SetStateFn fn = [] {
        const char* dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
        for (const char* d : dlls)
            if (void* h = LoadLibraryA(d))
                if (void* p = GetProcAddress(h, "XInputSetState")) return (SetStateFn)p;
        return (SetStateFn) nullptr;
    }();
    return fn;
}
// left = low-frequency (heavy) motor, right = high-frequency (light) motor, 0..1
inline void set(int pad, float left, float right) {
    if (SetStateFn f = setState()) {
        XInputVibration v{ (unsigned short)(std::clamp(left, 0.0f, 1.0f) * 65535), (unsigned short)(std::clamp(right, 0.0f, 1.0f) * 65535) };
        f((unsigned long)pad, &v);
    }
}
}
#else
namespace rumble { inline void set(int, float, float) {} }
#endif

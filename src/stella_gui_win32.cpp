// C:\workspace\Stella AI Studio\src\stella_gui_win32.cpp
//
// The exported plugin's window on Windows: a child window inside the host's, showing the
// Editor's pixels and passing the mouse to it. Redrawn about 30 times a second so meters,
// scopes and automation stay live.

#include "stella_gui.h"

#if defined (_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace stella::gui
{
    namespace
    {
        int liveWindows = 0;

        HMODULE thisModule()
        {
            HMODULE module = nullptr;
            GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR> (&thisModule), &module);
            return module;
        }

        /** One window class per plugin file, so two Stella plugins in one host never mix. */
        std::wstring className()
        {
            return L"StellaPluginWindow" + std::to_wstring (reinterpret_cast<std::uintptr_t> (thisModule()));
        }

        int pointerX (LPARAM l)   { return (int) (short) LOWORD (l); }
        int pointerY (LPARAM l)   { return (int) (short) HIWORD (l); }
        bool fineKeys (WPARAM w)  { return (w & (MK_SHIFT | MK_CONTROL)) != 0; }

        class Win32Window final : public Window
        {
        public:
            explicit Win32Window (Host& host)
                : editor (host)
            {
                pixels.resize ((size_t) editor.getWidth() * (size_t) editor.getHeight());

                if (liveWindows++ == 0)
                {
                    WNDCLASSEXW wc {};
                    wc.cbSize = sizeof (wc);
                    wc.style = CS_DBLCLKS;
                    wc.lpfnWndProc = proc;
                    wc.hInstance = thisModule();
                    wc.hCursor = LoadCursorW (nullptr, MAKEINTRESOURCEW (32512));   // IDC_ARROW, as a wide resource id
                    static std::wstring name;
                    name = className();
                    wc.lpszClassName = name.c_str();
                    RegisterClassExW (&wc);
                }
            }

            ~Win32Window() override
            {
                if (hwnd != nullptr)
                {
                    KillTimer (hwnd, 1);
                    SetWindowLongPtrW (hwnd, GWLP_USERDATA, 0);
                    DestroyWindow (hwnd);
                }

                if (--liveWindows == 0)
                    UnregisterClassW (className().c_str(), thisModule());
            }

            bool attach (void* parent) override
            {
                if (hwnd != nullptr)
                    return false;

                hwnd = CreateWindowExW (0, className().c_str(), L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                        0, 0, editor.getWidth(), editor.getHeight(), static_cast<HWND> (parent), nullptr,
                                        thisModule(), this);

                if (hwnd == nullptr)
                    return false;

                SetTimer (hwnd, 1, 33, nullptr);
                return true;
            }

            void setVisible (bool visible) override
            {
                if (hwnd != nullptr)
                    ShowWindow (hwnd, visible ? SW_SHOW : SW_HIDE);
            }

        private:
            static LRESULT CALLBACK proc (HWND window, UINT message, WPARAM w, LPARAM l)
            {
                if (message == WM_NCCREATE)
                {
                    auto* created = reinterpret_cast<CREATESTRUCTW*> (l);
                    SetWindowLongPtrW (window, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (created->lpCreateParams));
                }

                auto* self = reinterpret_cast<Win32Window*> (GetWindowLongPtrW (window, GWLP_USERDATA));

                if (self == nullptr)
                    return DefWindowProcW (window, message, w, l);

                switch (message)
                {
                    case WM_ERASEBKGND:
                        return 1;

                    case WM_PAINT:
                    {
                        PAINTSTRUCT paint;
                        auto* dc = BeginPaint (window, &paint);
                        self->editor.render (self->pixels.data());

                        BITMAPINFO info {};
                        info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
                        info.bmiHeader.biWidth = self->editor.getWidth();
                        info.bmiHeader.biHeight = -self->editor.getHeight();   // top row first
                        info.bmiHeader.biPlanes = 1;
                        info.bmiHeader.biBitCount = 32;
                        info.bmiHeader.biCompression = BI_RGB;

                        StretchDIBits (dc, 0, 0, self->editor.getWidth(), self->editor.getHeight(),
                                       0, 0, self->editor.getWidth(), self->editor.getHeight(),
                                       self->pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
                        EndPaint (window, &paint);
                        return 0;
                    }

                    case WM_TIMER:
                        InvalidateRect (window, nullptr, FALSE);
                        return 0;

                    case WM_LBUTTONDOWN:
                        SetCapture (window);
                        self->editor.mouseDown (pointerX (l), pointerY (l), fineKeys (w), false);
                        return 0;

                    case WM_LBUTTONDBLCLK:
                        self->editor.mouseDown (pointerX (l), pointerY (l), fineKeys (w), true);
                        return 0;

                    case WM_MOUSEMOVE:
                        if ((w & MK_LBUTTON) != 0)
                        {
                            self->editor.mouseDrag (pointerX (l), pointerY (l), fineKeys (w));
                        }
                        else
                        {
                            // Hovering: programmed elements hear it, and when it leaves.
                            if (! self->tracking)
                            {
                                TRACKMOUSEEVENT track {};
                                track.cbSize = sizeof (track);
                                track.dwFlags = TME_LEAVE;
                                track.hwndTrack = window;
                                self->tracking = TrackMouseEvent (&track) != 0;
                            }

                            self->editor.mouseMove (pointerX (l), pointerY (l));
                        }
                        return 0;

                    case WM_MOUSELEAVE:
                        self->tracking = false;
                        self->editor.mouseExit();
                        return 0;

                    case WM_LBUTTONUP:
                        ReleaseCapture();
                        self->editor.mouseUp();
                        return 0;

                    case WM_CAPTURECHANGED:
                        self->editor.mouseUp();
                        return 0;

                    case WM_MOUSEWHEEL:
                    {
                        POINT point { pointerX (l), pointerY (l) };
                        ScreenToClient (window, &point);
                        self->editor.mouseWheel (point.x, point.y, (float) GET_WHEEL_DELTA_WPARAM (w) / (float) WHEEL_DELTA);
                        return 0;
                    }

                    default:
                        break;
                }

                return DefWindowProcW (window, message, w, l);
            }

            Editor editor;
            std::vector<std::uint32_t> pixels;
            HWND hwnd = nullptr;
            bool tracking = false;   // asked to hear when the mouse leaves
        };
    }

    std::unique_ptr<Window> Window::create (Host& host)
    {
        return std::make_unique<Win32Window> (host);
    }
}

#else

namespace stella::gui
{
    std::unique_ptr<Window> Window::create (Host&)
    {
        return nullptr;
    }
}

#endif

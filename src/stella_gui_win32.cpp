// C:\workspace\Stella AI Studio\src\stella_gui_win32.cpp
//
// The exported plugin's window on Windows: a child window inside the host's, showing the
// Editor's pixels and passing the mouse to it. Redrawn about 30 times a second so meters,
// scopes and automation stay live. A protected plugin's "Unregistered" badge opens the
// registration window: the Machine ID to send, and a box for the serial number.

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

#if STELLA_PROTECTED
 #include "stella_licence.h"
#endif

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

       #if STELLA_PROTECTED
        //======================================================================
        // The registration window: a small window of the system's own controls, so typing,
        // pasting and copying work as everywhere in Windows (and the host's keyboard
        // shortcuts stay out of it).
        std::wstring wide (const std::string& text)
        {
            if (text.empty())
                return {};

            const auto size = MultiByteToWideChar (CP_UTF8, 0, text.c_str(), (int) text.size(), nullptr, 0);
            std::wstring result ((size_t) size, L'\0');
            MultiByteToWideChar (CP_UTF8, 0, text.c_str(), (int) text.size(), result.data(), size);
            return result;
        }

        std::wstring registrationClassName()
        {
            return L"StellaRegistration" + std::to_wstring (reinterpret_cast<std::uintptr_t> (thisModule()));
        }

        enum : int { copyId = 101, serialId = 102, registerId = 103, statusId = 104, closeId = 105 };

        class Registration
        {
        public:
            /** Opens it over the plugin's window (or brings it to the front if it's open). */
            static void open (HWND pluginWindow, HWND& slot)
            {
                if (slot != nullptr)
                {
                    SetForegroundWindow (slot);
                    return;
                }

                static bool registered = false;

                if (! registered)
                {
                    WNDCLASSEXW wc {};
                    wc.cbSize = sizeof (wc);
                    wc.lpfnWndProc = proc;
                    wc.hInstance = thisModule();
                    wc.hCursor = LoadCursorW (nullptr, MAKEINTRESOURCEW (32512));
                    wc.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);
                    static std::wstring name;
                    name = registrationClassName();
                    wc.lpszClassName = name.c_str();
                    registered = RegisterClassExW (&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
                }

                const int width = 460, height = 270;
                RECT frame { 0, 0, width, height };
                const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
                AdjustWindowRectEx (&frame, style, FALSE, WS_EX_DLGMODALFRAME);

                // Centred over the plugin's window.
                RECT over {};
                GetWindowRect (pluginWindow, &over);
                const int w = frame.right - frame.left, h = frame.bottom - frame.top;
                const int x = (over.left + over.right) / 2 - w / 2, y = (over.top + over.bottom) / 2 - h / 2;

                const auto title = wide (stella::info::name) + L" \u2013 Registration";
                slot = CreateWindowExW (WS_EX_DLGMODALFRAME, registrationClassName().c_str(), title.c_str(), style,
                                        x, y, w, h, GetAncestor (pluginWindow, GA_ROOT), nullptr, thisModule(), &slot);

                if (slot != nullptr)
                {
                    ShowWindow (slot, SW_SHOW);
                    SetForegroundWindow (slot);
                }
            }

        private:
            struct State
            {
                HWND* slot = nullptr;
                HFONT font = nullptr, bold = nullptr;
                WNDPROC editProc = nullptr;
            };

            static HWND add (HWND parent, const wchar_t* type, const std::wstring& text, DWORD style, int x, int y, int w, int h,
                             int id, HFONT font, DWORD extended = 0)
            {
                auto* control = CreateWindowExW (extended, type, text.c_str(), WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                                                 (HMENU) (INT_PTR) id, thisModule(), nullptr);
                SendMessageW (control, WM_SETFONT, (WPARAM) font, TRUE);
                return control;
            }

            /** Enter in the serial box registers, as the button does; Ctrl+A selects what was typed. */
            static LRESULT CALLBACK serialProc (HWND edit, UINT message, WPARAM w, LPARAM l)
            {
                auto* state = reinterpret_cast<State*> (GetWindowLongPtrW (GetParent (edit), GWLP_USERDATA));

                if (message == WM_KEYDOWN && w == VK_RETURN)
                {
                    SendMessageW (GetParent (edit), WM_COMMAND, MAKEWPARAM (registerId, BN_CLICKED), 0);
                    return 0;
                }

                if (message == WM_CHAR && (w == L'\r' || w == L'\n'))
                    return 0;

                if (message == WM_CHAR && w == 1)   // Ctrl+A selects it all (older edit boxes don't)
                {
                    SendMessageW (edit, EM_SETSEL, 0, -1);
                    return 0;
                }

                return state != nullptr && state->editProc != nullptr ? CallWindowProcW (state->editProc, edit, message, w, l)
                                                                       : DefWindowProcW (edit, message, w, l);
            }

            static void showRegistered (HWND window)
            {
                SetDlgItemTextW (window, statusId, (L"Thank you! " + wide (stella::info::name) + L" is registered.").c_str());
                EnableWindow (GetDlgItem (window, serialId), FALSE);
                EnableWindow (GetDlgItem (window, registerId), FALSE);
            }

            static LRESULT CALLBACK proc (HWND window, UINT message, WPARAM w, LPARAM l)
            {
                auto* state = reinterpret_cast<State*> (GetWindowLongPtrW (window, GWLP_USERDATA));

                switch (message)
                {
                    case WM_CREATE:
                    {
                        auto* created = reinterpret_cast<CREATESTRUCTW*> (l);
                        state = new State();
                        state->slot = static_cast<HWND*> (created->lpCreateParams);
                        state->font = CreateFontW (-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                        state->bold = CreateFontW (-17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                        SetWindowLongPtrW (window, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (state));

                        const auto intro = wide (stella::info::name) + L" isn't registered yet, so it plays in demo mode (3 seconds of "
                                           L"silence every 21). To register, send your Machine ID to " + wide (stella::info::vendor)
                                           + L" and type the serial number you get below.";

                        add (window, L"STATIC", intro, SS_LEFT, 20, 16, 420, 60, 0, state->font);
                        add (window, L"STATIC", L"Machine ID", SS_LEFT, 20, 88, 120, 20, 0, state->font);
                        add (window, L"EDIT", wide (stella::licence::machineId()), ES_READONLY | ES_CENTER, 140, 84, 180, 28, 0,
                             state->bold, WS_EX_CLIENTEDGE);
                        add (window, L"BUTTON", L"Copy", BS_PUSHBUTTON | WS_TABSTOP, 332, 84, 108, 28, copyId, state->font);

                        add (window, L"STATIC", L"Serial number", SS_LEFT, 20, 132, 120, 20, 0, state->font);
                        auto* serial = add (window, L"EDIT", L"", ES_CENTER | ES_AUTOHSCROLL | WS_TABSTOP, 140, 128, 180, 28, serialId,
                                            state->bold, WS_EX_CLIENTEDGE);
                        SendMessageW (serial, EM_LIMITTEXT, 24, 0);
                        state->editProc = reinterpret_cast<WNDPROC> (SetWindowLongPtrW (serial, GWLP_WNDPROC,
                                                                                       reinterpret_cast<LONG_PTR> (serialProc)));
                        add (window, L"BUTTON", L"Register", BS_DEFPUSHBUTTON | WS_TABSTOP, 332, 128, 108, 28, registerId, state->font);

                        add (window, L"STATIC", L"", SS_LEFT, 20, 176, 420, 40, statusId, state->font);
                        add (window, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, 332, 226, 108, 28, closeId, state->font);

                        if (stella::licence::isRegistered())
                            showRegistered (window);
                        else
                            SetFocus (serial);

                        return 0;
                    }

                    case WM_COMMAND:
                    {
                        const auto id = (int) LOWORD (w);

                        if (id == copyId && OpenClipboard (window))
                        {
                            const auto text = wide (stella::licence::machineId());
                            EmptyClipboard();

                            if (auto memory = GlobalAlloc (GMEM_MOVEABLE, (text.size() + 1) * sizeof (wchar_t)))
                            {
                                std::memcpy (GlobalLock (memory), text.c_str(), (text.size() + 1) * sizeof (wchar_t));
                                GlobalUnlock (memory);
                                SetClipboardData (CF_UNICODETEXT, memory);
                            }

                            CloseClipboard();
                            SetDlgItemTextW (window, statusId, L"The Machine ID is copied: paste it into your message.");
                        }
                        else if (id == registerId)
                        {
                            wchar_t typed[64] {};
                            GetDlgItemTextW (window, serialId, typed, 63);

                            std::string ascii;

                            for (const wchar_t* c = typed; *c != 0; ++c)
                                ascii += *c < 128 ? (char) *c : '?';

                            if (stella::licence::tryRegister (ascii))
                                showRegistered (window);
                            else
                                SetDlgItemTextW (window, statusId, L"That serial number doesn't match this computer's Machine ID. "
                                                                   L"Check it and try again.");
                        }
                        else if (id == closeId)
                        {
                            DestroyWindow (window);
                        }

                        return 0;
                    }

                    case WM_CLOSE:
                        DestroyWindow (window);
                        return 0;

                    case WM_DESTROY:
                        if (state != nullptr)
                        {
                            if (state->slot != nullptr)
                                *state->slot = nullptr;   // the plugin's window knows it's gone

                            DeleteObject (state->font);
                            DeleteObject (state->bold);
                            SetWindowLongPtrW (window, GWLP_USERDATA, 0);
                            delete state;
                        }
                        return 0;

                    default:
                        break;
                }

                return DefWindowProcW (window, message, w, l);
            }
        };
       #endif

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
               #if STELLA_PROTECTED
                if (registration != nullptr)
                    DestroyWindow (registration);   // it clears `registration` as it goes
               #endif

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
                        self->checkRegistration();
                        return 0;

                    case WM_LBUTTONDBLCLK:
                        self->editor.mouseDown (pointerX (l), pointerY (l), fineKeys (w), true);
                        self->checkRegistration();
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

            /** The badge was clicked: the registration window opens. */
            void checkRegistration()
            {
                if (! editor.takeRegistrationRequest())
                    return;

               #if STELLA_PROTECTED
                ReleaseCapture();
                Registration::open (hwnd, registration);
               #endif
            }

            Editor editor;
            std::vector<std::uint32_t> pixels;
            HWND hwnd = nullptr;
            bool tracking = false;   // asked to hear when the mouse leaves

           #if STELLA_PROTECTED
            HWND registration = nullptr;   // the registration window, while it's open
           #endif
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

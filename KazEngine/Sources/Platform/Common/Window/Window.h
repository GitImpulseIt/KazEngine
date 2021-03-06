#pragma once

#include <EventEmitter.hpp>
#include "IWindowListener.h"
#include "../Mouse/Mouse.h"
#include "../Keyboard/Keyboard.h"

#if defined(_WIN32)
#include <map>
#pragma comment(lib,"Version.lib")
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <locale>
#include <codecvt>
#include <string>

#elif defined(VK_USE_PLATFORM_XCB_KHR)
#include <xcb/xcb.h>
#include <dlfcn.h>
#include <cstdlib>

#elif defined(VK_USE_PLATFORM_XLIB_KHR)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#include <cstdlib>

#endif

namespace Engine
{

    #if defined(_WIN32)
    typedef HMODULE LibraryHandle;

    #elif defined(VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_XLIB_KHR)
    typedef void* LibraryHandle;

    #endif

    struct Surface
    {
        unsigned int width;
        unsigned int height;

        #if defined(VK_USE_PLATFORM_WIN32_KHR)
        HINSTANCE           instance;
        HWND                handle;
        Surface() : width(0), height(0), instance(nullptr), handle(nullptr) {}

        #elif defined(VK_USE_PLATFORM_XCB_KHR)
        xcb_connection_t*   connection;
        xcb_window_t        handle;
        Surface() : width(0), height(0), connection(nullptr), handle(nullptr) {}

        #elif defined(VK_USE_PLATFORM_XLIB_KHR)
        Display*            display;
        Window              handle;
        Surface() : width(0), height(0), display(nullptr), handle(nullptr) {}

        #endif
    };

    class Window : public Tools::EventEmitter<IWindowListener>
    {
        public:

            enum FULL_SCREEN_MODE {
                FULLSCREEN_MODE_ENABLED = true,
                FULLSCREEN_MODE_DISABLED = false
            };

            enum CURSOR_TYPE {
                CURSOR_DEFAULT  = 0,
                CURSOR_HAND     = 1,
                CURSOR_NO       = 2
            };

            Window(Area<uint32_t> const& window_size, std::string const& title, FULL_SCREEN_MODE full_screen);
            ~Window();

            IWindowListener::E_WINDOW_STATE GetWindowState();
            Surface GetSurface();
            static bool Loop();
            void SetTitle(std::string const& title);
            static void SetMouseCursor(CURSOR_TYPE cursor);
            bool ToggleFullScreen();

            void Center();
            void Hide();
            void Show();

        private:

            // Lock width/height ratio when resize
            float LockRatio = 4.0f / 3.0f;

            // Window position and size
            Rect<uint32_t> mem_rect;

            #if defined(VK_USE_PLATFORM_WIN32_KHR)
            HWND hWnd;                                                          // Windows API HWND
            std::string Title;                                                  // Titre de la fen�tre
            IWindowListener::E_WINDOW_STATE WindowState;                        // Etat de la fen�tre
            static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);     // Gestion des messages MS Windows
            static std::map<HWND, Window*> WindowList;                          // Contient la liste de toutes les fen�tres cr�es
            #endif
    };
}

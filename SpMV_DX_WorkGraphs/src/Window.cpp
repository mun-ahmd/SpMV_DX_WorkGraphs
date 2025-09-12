// This file is part of the AMD & HSC Work Graph Playground.
//
// Copyright (C) 2025 Advanced Micro Devices, Inc. and Coburg University of Applied Sciences and Arts.
// All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "Window.h"

#include <backends/imgui_impl_win32.h>

constexpr static const wchar_t* WindowClassName = L"SampleWindowClass";

Window::Window(const std::wstring& title, std::uint32_t width, std::uint32_t height) : width_(width), height_(height)
{
    const auto hInstance = GetModuleHandleW(NULL);

    WNDCLASSEXW windowClass   = {0};
    windowClass.cbSize        = sizeof(WNDCLASSEXW);
    windowClass.style         = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc   = MessageProc;
    windowClass.hInstance     = hInstance;
    windowClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = WindowClassName;
    RegisterClassExW(&windowClass);

    // Create the window and store a handle to it.
    hwnd_ = CreateWindowW(windowClass.lpszClassName,
                          title.c_str(),
                          WS_OVERLAPPEDWINDOW,
                          100,
                          100,
                          width,
                          height,
                          NULL,  // We have no parent window.
                          NULL,  // We aren't using menus.
                          hInstance,
                          this);

    // Show window
    ShowWindow(hwnd_, SW_NORMAL);
    UpdateWindow(hwnd_);
}

Window::~Window()
{
    DestroyWindow(hwnd_);

    const auto hInstance = GetModuleHandleW(NULL);
    UnregisterClassW(WindowClassName, hInstance);
}

void Window::Close()
{
    PostMessageA(hwnd_, WM_CLOSE, 0, 0);
}

bool Window::HandleEvents()
{
    MSG  msg  = {};
    bool quit = false;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT) {
            quit = true;
        }
    }

    return !quit;
}

HWND Window::GetHandle() const
{
    return hwnd_;
}

std::uint32_t Window::GetWidth() const
{
    return width_;
}

std::uint32_t Window::GetHeight() const
{
    return height_;
}

// Forward-declaration of ImGui Message Proc Handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Window Message Proc Handler
LRESULT WINAPI Window::MessageProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_CREATE: {
        // Save the Window instance pointer passed in to CreateWindow.
        LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
    }
        return 0;
    case WM_SIZE: {
        Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

        // Update width & height in window instance. Swapchain resizing will be handled in main loop.
        window->width_  = LOWORD(lParam);
        window->height_ = HIWORD(lParam);
    }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)  // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
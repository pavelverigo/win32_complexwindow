#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <winuser.h>

const int global_shift_timer_id = 1;

LARGE_INTEGER global_counter_frequency;
float Win32ElapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) {
    LARGE_INTEGER elapsed;
    elapsed.QuadPart = end.QuadPart - start.QuadPart;
    return (elapsed.QuadPart * 1000.0f) / global_counter_frequency.QuadPart;
}

struct offscreen_buffer {
    BITMAPINFO bitmap_info;
    void *data;
    int width;
    int height;
    int pitch;
};

struct window_dimension {
    int width;
    int height;
};

struct window_data {
    offscreen_buffer back_buffer;
    LARGE_INTEGER last_paint_time;
    int x_offset = 0;
    int y_offset = 0;
};

window_data global;

window_dimension
Win32GetWindowDimension(HWND Window) {
    window_dimension result;
    RECT client_rect;
    GetClientRect(Window, &client_rect);                        
    result.width = client_rect.right - client_rect.left;
    result.height = client_rect.bottom - client_rect.top;
    return result;
}

void
RenderWeirdGradient(offscreen_buffer buffer, int x_offset, int y_offset) {   
    uint8_t *row = (uint8_t *)buffer.data;
    for (int y = 0; y < buffer.height; ++y) {
        uint32_t *pixel = (uint32_t *)row;
        for (int x = 0; x < buffer.width; ++x) {
            uint8_t blue = (x + x_offset);
            uint8_t green = (y + y_offset);
            
            *pixel++ = ((green << 8) | blue);
        }

        row += buffer.pitch;
    }
}

void
Win32ResizeDIBSection(offscreen_buffer *buffer, int width, int height) {
    if (buffer->data) {
        VirtualFree(buffer->data, 0, MEM_RELEASE);
    }
    
    buffer->width = width;
    buffer->height = height;
    
    const int bytes_per_pixel = 4;
    
    buffer->bitmap_info.bmiHeader.biSize = sizeof(buffer->bitmap_info.bmiHeader);
    buffer->bitmap_info.bmiHeader.biWidth = buffer->width;
    buffer->bitmap_info.bmiHeader.biHeight = -buffer->height;
    buffer->bitmap_info.bmiHeader.biPlanes = 1;
    buffer->bitmap_info.bmiHeader.biBitCount = 32;
    buffer->bitmap_info.bmiHeader.biCompression = BI_RGB;

    int memory_size = (buffer->width * buffer->height) * bytes_per_pixel;
    buffer->data = VirtualAlloc(0, memory_size, MEM_COMMIT, PAGE_READWRITE);

    buffer->pitch = width * bytes_per_pixel;
}

void
Win32DisplayBufferInWindow(HDC device_ctx, int window_width, int window_height, offscreen_buffer buffer) {
    SetStretchBltMode(device_ctx, STRETCH_DELETESCANS);
    StretchDIBits(device_ctx,
                  /*
                  X, Y, Width, Height,
                  X, Y, Width, Height,
                  */
                  0, 0, window_width, window_height,
                  0, 0, buffer.width, buffer.height,
                  buffer.data,
                  &buffer.bitmap_info,
                  DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK
Win32MainWindowCallback(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    LRESULT result = 0;
    switch(msg) {
        case WM_DESTROY: {
            PostQuitMessage(0);
         } break;

        case WM_TIMER: {
            if (wparam == global_shift_timer_id) {
                global.x_offset += 1;
                global.y_offset += 2;
                result = 0;
                InvalidateRect(window, NULL, TRUE);
            } else {
                result = DefWindowProc(window, msg, wparam, lparam);
            }
        } break;

        case WM_PAINT: {
            RenderWeirdGradient(global.back_buffer, global.x_offset, global.y_offset);

            PAINTSTRUCT ps;
            HDC device_ctx = BeginPaint(window, &ps);
            
            window_dimension dim = Win32GetWindowDimension(window);

            HDC memory_device_ctx = CreateCompatibleDC(device_ctx);
            HBITMAP memory_bitmap = CreateCompatibleBitmap(device_ctx, dim.width, dim.height);
            SelectObject(memory_device_ctx, memory_bitmap);

            { // drawing in memory device ctx to avoid flickering
                Win32DisplayBufferInWindow(memory_device_ctx,
                                        dim.width,
                                        dim.height,
                                        global.back_buffer);

                LARGE_INTEGER new_time;
                QueryPerformanceCounter(&new_time);
                float fps = 1000.0f / Win32ElapsedMs(global.last_paint_time, new_time);
                global.last_paint_time = new_time;

                char text[64];
                sprintf(text, "fps: %.2f", fps);

                RECT text_rect = {0, 0, 100, 50};
                DrawText(memory_device_ctx, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            BitBlt(device_ctx, 0, 0, dim.width, dim.height, memory_device_ctx, 0, 0, SRCCOPY);
            DeleteObject(memory_bitmap);
            DeleteDC(memory_device_ctx);

            EndPaint(window, &ps);
        } break;

        case WM_SETCURSOR: {
            if (LOWORD(lparam) == HTCLIENT) {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                result = 1;
            } else {
                result = DefWindowProc(window, msg, wparam, lparam);
            }
        } break;

        default: {
            result = DefWindowProc(window, msg, wparam, lparam);
        } break;
    }
    return result;
}

int CALLBACK
WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int show_cmd) {
    {
        AllocConsole();
        FILE* pConStdOut;
        freopen_s(&pConStdOut, "CONOUT$", "w", stdout);
        FILE* pConStdErr;
        freopen_s(&pConStdErr, "CONOUT$", "w", stderr);
    }
    {
        QueryPerformanceFrequency(&global_counter_frequency);
        QueryPerformanceCounter(&global.last_paint_time);
    }
    {
        Win32ResizeDIBSection(&global.back_buffer, 1280, 720);
    }

    WNDCLASS window_class = {};
    window_class.style = CS_OWNDC|CS_HREDRAW|CS_VREDRAW;
    window_class.lpfnWndProc = Win32MainWindowCallback;
    window_class.hInstance = instance;
    window_class.lpszClassName = "MyWindowClass";

    if (!RegisterClassA(&window_class)) {
        return EXIT_FAILURE;
    }

    RECT rect = { 0, 0, 640, 640 };

    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW|WS_VISIBLE, FALSE, 0);

    HWND window = CreateWindowExA(0,
                            window_class.lpszClassName,
                            "My window",
                            WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                            CW_USEDEFAULT,
                            CW_USEDEFAULT,
                            rect.right - rect.left,
                            rect.bottom - rect.top,
                            0,
                            0,
                            instance,
                            0);
    if(!window) {
        return EXIT_FAILURE;
    }

    SetTimer(window, global_shift_timer_id, 10, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
        
    return EXIT_SUCCESS;
}

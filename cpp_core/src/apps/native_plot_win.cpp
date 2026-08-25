#include "native_plot_win.hpp"

#ifdef _WIN32

#include "tactics/board/board.hpp"
#include "tactics/board/board_display.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace tactics::apps {

namespace {

struct DrawEntity {
    int x{};
    int y{};
    int owner{};
    std::string label;
};

struct PlotState {
    tactics::BoardCellBounds bounds{};
    int width{1};
    int height{1};
    std::vector<DrawEntity> entities;
};

LRESULT CALLBACK BoardWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
        auto* state = reinterpret_cast<PlotState*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    auto* state = reinterpret_cast<PlotState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_PAINT: {
            if (!state) break;
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rect{};
            GetClientRect(hwnd, &rect);
            const int margin = 30;
            const int board_w = (rect.right - rect.left) - margin * 2;
            const int board_h = (rect.bottom - rect.top) - margin * 2;
            const int cell_w = board_w / state->width;
            const int cell_h = board_h / state->height;

            HBRUSH bg = CreateSolidBrush(RGB(250, 250, 250));
            FillRect(hdc, &rect, bg);
            DeleteObject(bg);

            HPEN grid_pen = CreatePen(PS_SOLID, 1, RGB(20, 20, 20));
            HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, grid_pen));

            for (int x = 0; x <= state->width; ++x) {
                const int px = margin + x * cell_w;
                MoveToEx(hdc, px, margin, nullptr);
                LineTo(hdc, px, margin + state->height * cell_h);
            }
            for (int y = 0; y <= state->height; ++y) {
                const int py = margin + y * cell_h;
                MoveToEx(hdc, margin, py, nullptr);
                LineTo(hdc, margin + state->width * cell_w, py);
            }

            SetBkMode(hdc, TRANSPARENT);
            for (int i = 0; i < state->width; ++i) {
                const int world_x = state->bounds.min_x + i;
                std::wstring s = std::to_wstring(world_x + 1);
                TextOutW(hdc, margin + i * cell_w + cell_w / 2 - 4, margin - 20, s.c_str(), static_cast<int>(s.size()));
            }
            for (int gy = 0; gy < state->height; ++gy) {
                const int world_y = state->bounds.min_y + gy;
                std::wstring s = std::to_wstring(world_y + 1);
                const int row = tactics::game_y_to_screen_row_top_origin(state->height, gy);
                const int py = margin + row * cell_h + cell_h / 2 - 6;
                TextOutW(hdc, margin - 20, py, s.c_str(), static_cast<int>(s.size()));
            }

            for (const auto& e : state->entities) {
                const int lx = e.x - state->bounds.min_x;
                const int ly = e.y - state->bounds.min_y;
                if (lx < 0 || ly < 0 || lx >= state->width || ly >= state->height) continue;

                const int left = tactics::cell_left_pixels(margin, cell_w, lx, tactics::kBoardEntityPixelInset);
                const int top = tactics::cell_top_pixels(margin, cell_h, state->height, ly, tactics::kBoardEntityPixelInset);
                const int right = left + tactics::cell_fill_extent_px(cell_w);
                const int bottom = top + tactics::cell_fill_extent_px(cell_h);

                COLORREF color = RGB(44, 62, 80);
                if (e.owner >= 1) {
                    const auto rgb = tactics::rgb_for_player_seat(e.owner);
                    color = RGB(rgb.r, rgb.g, rgb.b);
                }

                HBRUSH brush = CreateSolidBrush(color);
                RECT er{left, top, right, bottom};
                FillRect(hdc, &er, brush);
                DeleteObject(brush);

                SetTextColor(hdc, RGB(255, 255, 255));
                std::wstring wlabel(e.label.begin(), e.label.end());
                TextOutW(hdc, left + 6, top + 6, wlabel.c_str(), static_cast<int>(wlabel.size()));
            }

            SelectObject(hdc, old_pen);
            DeleteObject(grid_pen);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

}  // namespace

void plot_board_native(const GameState& game) {
    const tactics::BoardCellBounds bb =
        tactics::cell_bounds_or_main_module(game.board_cell_bounds(), game.board_width(), game.board_height());
    const int w = bb.span_x();
    const int h = bb.span_y();
    if (w < 1 || h < 1) return;

    PlotState state;
    state.bounds = bb;
    state.width = w;
    state.height = h;

    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->position) continue;
        DrawEntity d;
        d.x = ent->position->first;
        d.y = ent->position->second;
        d.owner = ent->owner.value_or(0);
        d.label = ent->entity_type;
        if (auto u = std::dynamic_pointer_cast<Unit>(ent)) d.label = u->unit_type;
        if (d.label.size() > 7) d.label = d.label.substr(0, 7);
        state.entities.push_back(std::move(d));
    }

    HINSTANCE hinst = GetModuleHandle(nullptr);
    const wchar_t* klass = L"TacticsBoardPlotWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = BoardWndProc;
    wc.hInstance = hinst;
    wc.lpszClassName = klass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        klass,
        L"Tactics Board (Native C++)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        900,
        nullptr,
        nullptr,
        hinst,
        &state);

    if (!hwnd) return;

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

}  // namespace tactics::apps

#else

namespace tactics::apps {

void plot_board_native(const GameState&) {}

}  // namespace tactics::apps

#endif

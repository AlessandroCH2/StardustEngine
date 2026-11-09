#pragma once
#include "Core/Base.h"
#include <glm/vec2.hpp>

struct GLFWwindow;

namespace Stardust {

// Key codes match GLFW so they can be passed straight through to C# scripts.
enum class Key : i32 {
    Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
    D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Escape = 256, Enter, Tab, Backspace, Insert, Delete, Right, Left, Down, Up,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift = 340, LeftControl, LeftAlt, LeftSuper, RightShift, RightControl, RightAlt,
};

enum class MouseButton : i32 { Left = 0, Right = 1, Middle = 2 };
enum class CursorMode { Normal, Hidden, Locked };

class Input {
public:
    static bool IsKeyDown(Key k);
    static bool IsKeyPressed(Key k);   // went down this frame
    static bool IsMouseDown(MouseButton b);
    static bool IsMousePressed(MouseButton b);
    static glm::vec2 MousePosition();
    static glm::vec2 MouseDelta();
    static float ScrollDelta();
    static void SetCursorMode(CursorMode mode);
    static CursorMode GetCursorMode(){return s_CursorMode;}
    static bool WindowFocused();
    static bool GamepadConnected(int index);
    static float GamepadAxis(int index,int axis);
    static bool GamepadButton(int index,int button,bool pressed=false);

    // Runtime reads are routed independently from editor navigation.
    static inline bool RuntimeKeyboard=true,RuntimeMouse=true;
    static inline glm::vec2 RuntimeOrigin{0};
    // engine internal
    static void Attach(GLFWwindow* window);
    static void BeginFrame();
    static void SampleMousePosition(glm::vec2 position); // backend sampling, also used by deterministic input tests
    static void ResetMouseTracking();

private:
    static inline GLFWwindow* s_Window = nullptr;
    static inline bool s_Keys[512]{}, s_PrevKeys[512]{};
    static inline bool s_Mouse[8]{}, s_PrevMouse[8]{};
    static inline bool s_PendingKeys[512]{}, s_PressedKeys[512]{};
    static inline bool s_PendingMouse[8]{}, s_PressedMouse[8]{};
    static inline glm::vec2 s_MousePos{}, s_PrevMousePos{};
    static inline float s_Scroll = 0, s_ScrollAccum = 0;
    static inline bool s_FirstMouse = true;
    static inline CursorMode s_CursorMode=CursorMode::Normal;
    static inline bool s_GamepadConnected[16]{};
    static inline float s_GamepadAxes[16][6]{};
    static inline bool s_GamepadButtons[16][15]{},s_PrevGamepadButtons[16][15]{};
};

} // namespace Stardust

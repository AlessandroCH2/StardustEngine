#include "Core/Input.h"
#include <GLFW/glfw3.h>
#include <cstring>

namespace Stardust {

void Input::Attach(GLFWwindow* window) {
    s_Window = window;s_FirstMouse=true;s_CursorMode=CursorMode::Normal;
    // Keep short presses that begin and end during a single event-poll batch.
    // ImGui installs chaining callbacks after Attach, so both consumers receive them.
    glfwSetKeyCallback(window, [](GLFWwindow*, int key, int, int action, int) {
        if (key >= 0 && key < 512 && action == GLFW_PRESS) s_PendingKeys[key] = true;
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow*, int button, int action, int) {
        if (button >= 0 && button < 8 && action == GLFW_PRESS) s_PendingMouse[button] = true;
    });
    // Chain scroll: ImGui's GLFW backend installs its callbacks later with chaining enabled, so ours stays called.
    glfwSetScrollCallback(window, [](GLFWwindow*, double, double y) { s_ScrollAccum += (float)y; });
}

void Input::BeginFrame() {
    std::memcpy(s_PrevKeys, s_Keys, sizeof(s_Keys));
    std::memcpy(s_PrevMouse, s_Mouse, sizeof(s_Mouse));
    s_Scroll = s_ScrollAccum;
    s_ScrollAccum = 0;

    if (!s_Window) return;
    for(int i=0;i<16;++i){
        std::memcpy(s_PrevGamepadButtons[i],s_GamepadButtons[i],sizeof(s_GamepadButtons[i]));
        GLFWgamepadstate state{};s_GamepadConnected[i]=glfwGetGamepadState(GLFW_JOYSTICK_1+i,&state)==GLFW_TRUE;
        for(int a=0;a<6;++a)s_GamepadAxes[i][a]=s_GamepadConnected[i]?state.axes[a]:0;
        for(int b=0;b<15;++b)s_GamepadButtons[i][b]=s_GamepadConnected[i]&&state.buttons[b]==GLFW_PRESS;
    }
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; ++k) s_Keys[k] = glfwGetKey(s_Window, k) == GLFW_PRESS;
    for (int b = 0; b < 8; ++b) s_Mouse[b] = glfwGetMouseButton(s_Window, b) == GLFW_PRESS;
    for (int k = 0; k < 512; ++k) { s_PressedKeys[k] = s_PendingKeys[k] || (s_Keys[k] && !s_PrevKeys[k]); s_PendingKeys[k] = false; }
    for (int b = 0; b < 8; ++b) { s_PressedMouse[b] = s_PendingMouse[b] || (s_Mouse[b] && !s_PrevMouse[b]); s_PendingMouse[b] = false; }
    double x, y;
    glfwGetCursorPos(s_Window, &x, &y);
    SampleMousePosition({(float)x,(float)y});
}

bool Input::IsKeyDown(Key k) { return (int)k>=0&&(int)k<512&&s_Keys[(int)k]; }
bool Input::GamepadConnected(int i){return i>=0&&i<16&&s_GamepadConnected[i];}
float Input::GamepadAxis(int i,int a){return RuntimeKeyboard&&GamepadConnected(i)&&a>=0&&a<6?s_GamepadAxes[i][a]:0;}
bool Input::GamepadButton(int i,int b,bool pressed){return RuntimeKeyboard&&GamepadConnected(i)&&b>=0&&b<15&&s_GamepadButtons[i][b]&&(!pressed||!s_PrevGamepadButtons[i][b]);}
bool Input::IsKeyPressed(Key k) { return (int)k>=0&&(int)k<512&&s_PressedKeys[(int)k]; }
bool Input::IsMouseDown(MouseButton b) { return (int)b>=0&&(int)b<8&&s_Mouse[(int)b]; }
bool Input::IsMousePressed(MouseButton b) { return (int)b>=0&&(int)b<8&&s_PressedMouse[(int)b]; }
void Input::ResetMouseTracking(){s_FirstMouse=true;s_PrevMousePos=s_MousePos;}
void Input::SampleMousePosition(glm::vec2 position){s_PrevMousePos=s_MousePos;s_MousePos=position;if(s_FirstMouse){s_PrevMousePos=position;s_FirstMouse=false;}}

glm::vec2 Input::MousePosition() { return s_MousePos; }
glm::vec2 Input::MouseDelta() { return s_MousePos - s_PrevMousePos; }
float Input::ScrollDelta() { return s_Scroll; }

bool Input::WindowFocused(){return s_Window&&glfwGetWindowAttrib(s_Window,GLFW_FOCUSED)==GLFW_TRUE;}
void Input::SetCursorMode(CursorMode mode) {
    if(!s_Window||s_CursorMode==mode)return;
    s_CursorMode=mode;s_FirstMouse=true;

    int m = mode == CursorMode::Normal ? GLFW_CURSOR_NORMAL : mode == CursorMode::Hidden ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_DISABLED;
    glfwSetInputMode(s_Window, GLFW_CURSOR, m);
    if(glfwRawMouseMotionSupported())glfwSetInputMode(s_Window,GLFW_RAW_MOUSE_MOTION,mode==CursorMode::Locked?GLFW_TRUE:GLFW_FALSE);
    double x,y;glfwGetCursorPos(s_Window,&x,&y);s_MousePos=s_PrevMousePos={(float)x,(float)y};
}

} // namespace Stardust

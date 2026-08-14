#include "platform.hpp"

namespace ember::native {
namespace {

float axis(SDL_Gamepad* pad, SDL_GamepadAxis key) {
    if (!pad) return 0;
    const float value = static_cast<float>(SDL_GetGamepadAxis(pad,key))/32767.0f;
    return std::copysign(std::max(0.0f,std::abs(value)-0.16f)/0.84f,value);
}

}

Platform::Platform(const char* title, int width, int height, bool audio) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) throw std::runtime_error(SDL_GetError());
    if (audio) SDL_InitSubSystem(SDL_INIT_AUDIO);
    window_ = SDL_CreateWindow(title,width,height,SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error(error);
    }
    SDL_SetWindowMinimumSize(window_,440,340);
    focused_ = (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

Platform::~Platform() {
    if (pad_) SDL_CloseGamepad(pad_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
}

Input Platform::input(const std::function<void(const SDL_Event&)>& observer) {
    Input input;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (observer) observer(event);
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            input.quit = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            input.resized = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            focused_ = false;
            input.lost_focus = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            focused_ = true;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!pad_) pad_ = SDL_OpenGamepad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (pad_ && SDL_GetGamepadID(pad_) == event.gdevice.which) {
                SDL_CloseGamepad(pad_);
                pad_ = nullptr;
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.repeat || !focused_) break;
            switch (event.key.scancode) {
            case SDL_SCANCODE_ESCAPE: input.pause = true; break;
            case SDL_SCANCODE_RETURN: input.enter = true; break;
            case SDL_SCANCODE_TAB: input.select = true; break;
            case SDL_SCANCODE_SPACE: input.jump = true; break;
            case SDL_SCANCODE_E: input.interact = true; break;
            case SDL_SCANCODE_F9: input.monitor = true; break;
            case SDL_SCANCODE_F11: input.fullscreen = true; break;
            case SDL_SCANCODE_UP: input.up = true; break;
            case SDL_SCANCODE_DOWN: input.down = true; break;
            case SDL_SCANCODE_LEFT: input.left = true; break;
            case SDL_SCANCODE_RIGHT: input.right = true; break;
            case SDL_SCANCODE_R: input.restart = true; break;
            case SDL_SCANCODE_F: input.follow = true; break;
            default: break;
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (!focused_ || !pad_ || SDL_GetGamepadID(pad_) != event.gbutton.which) break;
            switch (event.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_SOUTH: input.jump = input.enter = true; break;
            case SDL_GAMEPAD_BUTTON_EAST: input.interact = true; break;
            case SDL_GAMEPAD_BUTTON_NORTH: input.select = true; break;
            case SDL_GAMEPAD_BUTTON_START: input.pause = true; break;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: input.follow = true; break;
            case SDL_GAMEPAD_BUTTON_DPAD_UP: input.up = true; break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: input.down = true; break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: input.left = true; break;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: input.right = true; break;
            default: break;
            }
            break;
        default: break;
        }
    }
    if (!focused_) return input;
    const auto* keys = SDL_GetKeyboardState(nullptr);
    input.movement.x = std::clamp(static_cast<float>(keys[SDL_SCANCODE_D]) -
                                 static_cast<float>(keys[SDL_SCANCODE_A])+axis(pad_,SDL_GAMEPAD_AXIS_LEFTX),-1.0f,1.0f);
    input.movement.z = std::clamp(static_cast<float>(keys[SDL_SCANCODE_S]) -
                                 static_cast<float>(keys[SDL_SCANCODE_W])+axis(pad_,SDL_GAMEPAD_AXIS_LEFTY),-1.0f,1.0f);
    input.crouch = keys[SDL_SCANCODE_LCTRL] || (pad_ && SDL_GetGamepadButton(pad_,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    return input;
}

void Platform::set_fullscreen(bool enabled) const {
    if (!SDL_SetWindowFullscreen(window_,enabled)) throw std::runtime_error(SDL_GetError());
}

std::filesystem::path executable_directory() {
    const char* path = SDL_GetBasePath();
    if (!path) throw std::runtime_error(SDL_GetError());
    return path;
}

std::filesystem::path profile_directory() {
    std::unique_ptr<char,decltype(&SDL_free)> path{SDL_GetPrefPath("Emberfall","The Lantern Reach"),SDL_free};
    if (!path) throw std::runtime_error(SDL_GetError());
    return path.get();
}

}

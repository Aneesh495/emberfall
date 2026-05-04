#pragma once

#include "platform.hpp"

namespace ember::native {

class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    void cue(Signal signal, float pan = 0, float attenuation = 1);
    void update(float volume);
    bool enabled() const { return stream_ != nullptr; }
private:
    struct Voice {
        float phase{}, frequency{}, slide{}, elapsed{}, duration{}, pan{}, gain{};
    };
    SDL_AudioStream* stream_{};
    std::array<Voice,12> voices_{};
};

}

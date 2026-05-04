#include "audio.hpp"

#include <numbers>

namespace ember::native {
namespace {
constexpr int sample_rate = 48000;
constexpr int samples = 512;
constexpr float tau = 2*std::numbers::pi_v<float>;
}

Audio::Audio() {
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) return;
    const SDL_AudioSpec spec{SDL_AUDIO_F32,2,sample_rate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if (stream_ && !SDL_ResumeAudioStreamDevice(stream_)) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

Audio::~Audio() {
    if (stream_) SDL_DestroyAudioStream(stream_);
}

void Audio::cue(Signal signal, float pan, float attenuation) {
    if (!stream_) return;
    auto found = std::ranges::find_if(voices_,[](const auto& v) { return v.elapsed >= v.duration; });
    if (found == voices_.end()) return;
    auto& voice = *found;
    voice = {};
    voice.pan = std::clamp(pan,-1.0f,1.0f);
    voice.gain = std::clamp(attenuation,0.0f,1.0f);
    switch (signal) {
    case Signal::latch: voice.frequency=163; voice.duration=0.32f; voice.slide=0.7f; break;
    case Signal::mirror: voice.frequency=440; voice.duration=0.25f; voice.slide=0.55f; break;
    case Signal::checkpoint: voice.frequency=385; voice.duration=0.65f; voice.slide=1.0f; break;
    case Signal::fall: voice.frequency=205; voice.duration=0.48f; voice.slide=-1.6f; break;
    case Signal::exit: voice.frequency=522; voice.duration=0.85f; voice.slide=0.6f; break;
    }
}

void Audio::update(float volume) {
    if (!stream_) return;
    for (int block=0; block<3; ++block) {
        const int queued = SDL_GetAudioStreamQueued(stream_);
        if (queued < 0 || queued >= 3*samples*2*static_cast<int>(sizeof(float))) return;
        std::array<float,samples*2> mix{};
        for (auto& voice : voices_) {
            const float left = std::sqrt((1-voice.pan)*0.5f);
            const float right = std::sqrt((1+voice.pan)*0.5f);
            for (std::size_t i=0; i<mix.size() && voice.elapsed<voice.duration; i+=2) {
                const float decay = 1-voice.elapsed/voice.duration;
                const float envelope = std::min(1.0f,voice.elapsed*75)*decay*decay;
                const float wave = std::sin(voice.phase) + 0.22f*std::sin(voice.phase*2);
                const float value = wave*envelope*voice.gain*volume*0.22f;
                mix[i] += value*left;
                mix[i+1] += value*right;
                voice.phase += tau*voice.frequency*std::exp2(voice.slide*voice.elapsed/voice.duration)/sample_rate;
                voice.elapsed += 1.0f/sample_rate;
            }
        }
        for (auto& sample : mix) sample = std::clamp(sample,-0.95f,0.95f);
        if (!SDL_PutAudioStreamData(stream_,mix.data(),static_cast<int>(sizeof(mix)))) return;
    }
}

}

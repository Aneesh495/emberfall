#include "audio.hpp"

#include <charconv>
#include <iostream>
#include <limits>

namespace ember::native {
namespace {

enum class Screen { title, playing, pause, settings, next, finished };

struct Options {
    std::filesystem::path assets, shaders, profile, capture;
    unsigned frames{}, level{};
    int width{1100}, height{700};
    bool validation{}, audio{true};
};

unsigned positive(std::string_view text) {
    unsigned result = 0;
    const auto parsed = std::from_chars(text.data(),text.data()+text.size(),result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size() || result == 0) {
        throw std::runtime_error("Expected a positive integer");
    }
    return result;
}

Options parse(int argc,char** argv) {
    Options options;
    for (int i=1; i<argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value = [&] {
            if (++i == argc) throw std::runtime_error("Missing value for " + std::string(arg));
            return argv[i];
        };
        if (arg == "--assets") options.assets = value();
        else if (arg == "--shaders") options.shaders = value();
        else if (arg == "--profile") options.profile = value();
        else if (arg == "--capture") options.capture = value();
        else if (arg == "--frames") options.frames = positive(value());
        else if (arg == "--level") options.level = positive(value());
        else if (arg == "--validate") options.validation = true;
        else if (arg == "--no-audio") options.audio = false;
        else if (arg == "--size") {
            const std::string_view dimensions = value();
            const auto split = dimensions.find('x');
            if (split == std::string_view::npos) throw std::runtime_error("Expected size WIDTHxHEIGHT");
            const auto w = positive(dimensions.substr(0,split)), h = positive(dimensions.substr(split+1));
            if (w<440 || h<340 || w>1920 || h>1080) throw std::runtime_error("Window size outside bounds");
            options.width = static_cast<int>(w);
            options.height = static_cast<int>(h);
        } else throw std::runtime_error("Unknown option: " + std::string(arg));
    }
    if (options.level > Campaign::level_count || options.frames > 10000 || (!options.capture.empty() && !options.frames)) {
        throw std::runtime_error("Level or capture frame outside bounds");
    }
    const auto base = executable_directory();
    if (options.assets.empty()) options.assets = base/"levels";
    if (options.shaders.empty()) options.shaders = base/"shaders";
    if (options.profile.empty()) options.profile = profile_directory();
    return options;
}

constexpr Color amber{1,0.52f,0.14f};
constexpr Color cyan{0.16f,0.82f,0.72f};
constexpr Color white{0.86f,0.92f,1};
constexpr Color muted{0.49f,0.61f,0.73f};

class Application {
public:
    explicit Application(const Options& options);
    int run();

private:
    void handle(Input input);
    void simulate(double elapsed);
    void trigger(GameEvent event);
    void load(Progress progress);
    void checkpoint();
    void persist();
    void paint(Canvas& canvas) const;
    void menu(Canvas& canvas, float y, std::span<const std::string_view> rows) const;

    const Options& options_;
    Platform platform_;
    Audio audio_;
    Renderer renderer_;
    Campaign campaign_;
    std::unique_ptr<Game> world_;
    Progress progress_;
    Settings settings_;
    Screen screen_{Screen::title}, previous_{Screen::title};
    unsigned cursor_{}, selected_{};
    Vec3 eye_{}, focus_{};
    Command pending_{};
    double accumulator_{};
    double note_time_{};
    std::string note_;
    bool quit_{};
};

Application::Application(const Options& options)
    : options_(options), platform_("The Lantern Reach",options.width,options.height,options.audio),
      renderer_(platform_.window(),options.shaders,options.validation), campaign_(options.assets) {
    std::filesystem::create_directories(options.profile);
    try {
        settings_ = read_settings(options.profile/"settings.save");
    } catch (const std::exception& error) {
        note_ = error.what();
    }
    try {
        progress_ = campaign_.read_progress(options.profile/"campaign.save");
    } catch (const std::exception& error) {
        note_ = error.what();
    }
    note_time_ = note_.empty() ? 0 : 12;
    platform_.set_fullscreen(settings_.fullscreen);
    world_ = campaign_.load({});
    eye_ = {5,11,16};
    focus_ = {0,0,0};
    if (options.level) load({options.level-1,0,false});
    std::cout << renderer_.adapter() << " audio=" << (audio_.enabled() ? "ready" : "unavailable") << '\n';
}

void Application::load(Progress progress) {
    if (progress.complete) {
        screen_ = Screen::finished;
        return;
    }
    world_ = campaign_.load(progress);
    progress_ = progress;
    screen_ = Screen::playing;
    selected_ = 0;
    focus_ = world_->player(selected_).body.feet + Vec3{0,0,-1.2f};
    eye_ = focus_ + Vec3{1.4f,7.2f,8.2f};
    accumulator_ = 0;
    pending_ = {};
}

void Application::checkpoint() {
    try {
        campaign_.save_progress(options_.profile/"campaign.save",progress_);
    } catch (const std::exception& error) {
        note_ = error.what();
        note_time_ = 6;
    }
}

void Application::persist() {
    try {
        save_settings(options_.profile/"settings.save",settings_);
    } catch (const std::exception& error) {
        note_ = error.what();
        note_time_ = 6;
    }
}

void Application::handle(Input input) {
    if (input.resized) renderer_.resize();
    if (input.quit) quit_ = true;
    if (input.fullscreen) {
        settings_.fullscreen = !settings_.fullscreen;
        platform_.set_fullscreen(settings_.fullscreen);
        persist();
    }
    if (screen_ == Screen::playing) {
        if (input.pause || (input.lost_focus && !options_.frames)) {
            screen_ = Screen::pause;
            cursor_ = 0;
            pending_ = {};
            return;
        }
        if (input.select) {
            selected_ ^= 1;
            pending_ = {};
        }
        if (input.restart) {
            load({progress_.level,world_->checkpoint(),false});
            return;
        }
        pending_.move = input.movement;
        pending_.crouch = input.crouch;
        pending_.jump = pending_.jump || input.jump;
        pending_.interact = pending_.interact || input.interact;
        return;
    }
    if (screen_ == Screen::next) {
        if (input.enter) load(progress_);
        if (input.pause) { screen_ = Screen::title; cursor_ = 0; }
        return;
    }
    if (screen_ == Screen::finished) {
        if (input.enter || input.pause) { screen_ = Screen::title; cursor_ = 0; }
        return;
    }
    const unsigned count = screen_ == Screen::settings ? 6U : 4U;
    if (input.up) cursor_ = (cursor_+count-1)%count;
    if (input.down) cursor_ = (cursor_+1)%count;
    if (screen_ == Screen::settings) {
        const float direction = input.left ? -1.0f : 1.0f;
        if (input.left || input.right || input.enter) {
            switch (cursor_) {
            case 0:
                settings_.volume = std::clamp(settings_.volume+direction*0.1f,0.0f,1.0f);
                audio_.cue(Signal::checkpoint);
                break;
            case 1: settings_.exposure = std::clamp(settings_.exposure+direction*0.1f,0.4f,2.5f); break;
            case 2: settings_.antialias = !settings_.antialias; break;
            case 3: settings_.high_contrast = !settings_.high_contrast; break;
            case 4:
                settings_.fullscreen = !settings_.fullscreen;
                platform_.set_fullscreen(settings_.fullscreen);
                break;
            case 5:
                if (input.enter) { screen_ = previous_; cursor_ = 0; }
                break;
            }
            persist();
        }
        if (input.pause) { screen_ = previous_; cursor_ = 0; }
    } else if (screen_ == Screen::title && input.enter) {
        if (cursor_ == 0) load(progress_);
        if (cursor_ == 1) { load({}); checkpoint(); }
        if (cursor_ == 2) { previous_ = screen_; screen_ = Screen::settings; cursor_ = 0; }
        if (cursor_ == 3) quit_ = true;
    } else if (screen_ == Screen::pause) {
        if (input.pause || (input.enter && cursor_ == 0)) screen_ = Screen::playing;
        else if (input.enter && cursor_ == 1) load({progress_.level,world_->checkpoint(),false});
        else if (input.enter && cursor_ == 2) { previous_ = screen_; screen_ = Screen::settings; cursor_ = 0; }
        else if (input.enter && cursor_ == 3) { screen_ = Screen::title; cursor_ = 0; }
    }
}

void Application::trigger(GameEvent event) {
    Vec3 origin = world_->player(selected_).body.feet;
    const auto& nodes = campaign_.scene(progress_.level).nodes;
    if (const auto found = nodes.find(event.subject); found != nodes.end()) origin = found->second.position;
    const auto offset = origin - world_->player(selected_).body.feet;
    audio_.cue(event.signal,std::clamp(offset.x/8,-1.0f,1.0f),1/(1+length(offset)*0.12f));
    switch (event.signal) {
    case Signal::latch: note_ = "Circuit energized"; break;
    case Signal::mirror: note_ = "Lantern rotated"; break;
    case Signal::checkpoint:
        progress_.checkpoint = event.subject;
        checkpoint();
        note_ = "Checkpoint saved";
        break;
    case Signal::fall: note_ = "Recovered at the last beacon"; break;
    case Signal::exit: note_ = "Route opened"; break;
    }
    note_time_ = 2.5;
}

void Application::simulate(double elapsed) {
    note_time_ = std::max(0.0,note_time_-elapsed);
    if (screen_ != Screen::playing) return;
    const auto cap = static_cast<double>(Game::timestep)*5;
    accumulator_ = std::min(accumulator_+elapsed,cap);
    while (accumulator_ >= Game::timestep && screen_ == Screen::playing) {
        std::array<Command,2> pair{};
        pair[selected_] = pending_;
        pending_.jump = pending_.interact = false;
        world_->tick(pair);
        accumulator_ -= Game::timestep;
        for (const auto event : world_->events()) trigger(event);
        if (world_->completed()) {
            const bool final = progress_.level+1 == Campaign::level_count;
            progress_ = {final ? progress_.level : progress_.level+1,0,final};
            checkpoint();
            screen_ = final ? Screen::finished : Screen::next;
        }
    }
    const auto target = world_->player(selected_).body.feet + Vec3{0,0,-1.2f};
    const auto weight = static_cast<float>(1-std::exp(-elapsed*7));
    focus_ = lerp(focus_,target,weight);
    eye_ = lerp(eye_,target+Vec3{1.4f,7.2f,8.2f},weight);
}

void Application::menu(Canvas& canvas, float y, std::span<const std::string_view> rows) const {
    const float width = std::min(440.0f,canvas.width()-68);
    for (std::size_t i=0; i<rows.size(); ++i) {
        const bool active = i == cursor_;
        canvas.box(32,y,width,36,active ? Color{0.15f,0.27f,0.29f,0.97f} : Color{0.045f,0.073f,0.11f,0.92f});
        if (active) canvas.box(32,y,4,36,cyan);
        canvas.text(49,y+11,2.1f,rows[i],active ? white : muted);
        y += 43;
    }
}

void Application::paint(Canvas& canvas) const {
    const auto panel = Color{0.012f,0.023f,0.047f,settings_.high_contrast ? 1.0f : 0.88f};
    if (screen_ == Screen::playing) {
        canvas.box(0,0,canvas.width(),95,panel);
        canvas.box(0,94,canvas.width(),1,Color{0.12f,0.33f,0.37f});
        const auto& scene = campaign_.scene(progress_.level);
        canvas.text(23,15,2.8f,scene.title,white);
        canvas.wrapped(23,50,1.75f,scene.objective,white);
        canvas.box(0,538,canvas.width(),62,panel);
        canvas.text(23,548,1.7f,selected_ ? "GLEAM   /   EMBER HOLDING" : "EMBER   /   GLEAM HOLDING",selected_ ? cyan : amber);
        canvas.wrapped(23,572,1.43f,"WASD MOVE   SPACE JUMP   E USE   TAB SWITCH   CTRL CROUCH   ESC PAUSE",muted);
        if (note_time_ > 0) {
            canvas.box(22,111,std::min(canvas.width()-44,370.0f),34,panel);
            canvas.wrapped(32,121,1.55f,note_.substr(0,120),cyan);
        }
        return;
    }
    const float panel_width = std::min(canvas.width()-28,520.0f);
    canvas.box(14,37,panel_width,526,panel);
    canvas.box(14,37,panel_width,3,cyan);
    canvas.text(32,64,2,"EMBERFALL   /   FIELD LOG",cyan);
    if (screen_ == Screen::title) {
        const auto title_size = std::min(4.0f,(panel_width-36)/102);
        canvas.text(32,106,title_size,"THE LANTERN REACH",white);
        canvas.wrapped(32,156,1.6f,"Two explorers. One dark observatory.",muted);
        constexpr std::array<std::string_view,4> rows{"Continue","New expedition","Settings","Quit"};
        menu(canvas,215,rows);
        canvas.wrapped(32,418,1.5f,"Walk and jump between platforms. Switch explorers to leave one on a relay.",white);
        canvas.wrapped(32,471,1.35f,"ARROWS OR D-PAD    ENTER OR A",muted);
        if (note_time_ > 0) canvas.wrapped(32,500,1.35f,note_.substr(0,120),amber);
    } else if (screen_ == Screen::pause) {
        canvas.text(32,106,4,"EXPEDITION PAUSED",white);
        constexpr std::array<std::string_view,4> rows{"Resume","Restart checkpoint","Settings","Return to title"};
        menu(canvas,197,rows);
    } else if (screen_ == Screen::settings) {
        canvas.text(32,106,4,"SETTINGS",white);
        const auto on = [](bool b) { return b ? "ON" : "OFF"; };
        const auto volume = "Volume " + std::to_string(static_cast<int>(std::round(settings_.volume*100)));
        const auto exposure = "Exposure " + std::to_string(static_cast<int>(std::round(settings_.exposure*100)));
        const auto edges = std::string("Edge filter ") + on(settings_.antialias);
        const auto contrast = std::string("High contrast ") + on(settings_.high_contrast);
        const auto fullscreen = std::string("Fullscreen ") + on(settings_.fullscreen);
        const std::array<std::string_view,6> rows{volume,exposure,edges,contrast,fullscreen,"Back"};
        menu(canvas,180,rows);
        canvas.wrapped(32,462,1.45f,"LEFT / RIGHT TO ADJUST",muted);
    } else {
        const bool finished = screen_ == Screen::finished;
        canvas.text(32,106,3.0f,finished ? "OBSERVATORY RELIT" : "ROUTE OPENED",white);
        canvas.wrapped(32,175,1.7f,finished ? "The beacon is lit. The night crew can find its way home." :
            campaign_.scene(progress_.level).title,cyan);
        canvas.wrapped(32,245,1.5f,finished ? "ENTER TO RETURN TO TITLE" : "ENTER TO CONTINUE",white);
    }
}

int Application::run() {
    auto previous = SDL_GetTicksNS();
    unsigned drawn = 0;
    const auto stop_at = previous+60'000'000'000ULL;
    while (!quit_) {
        const auto now = SDL_GetTicksNS();
        const auto elapsed = std::clamp(static_cast<double>(now-previous)*1e-9,0.0,0.1);
        previous = now;
        handle(platform_.input());
        if (quit_) break;
        simulate(elapsed);
        audio_.update(settings_.volume);
        int width = 0, height = 0;
        if (!SDL_GetWindowSize(platform_.window(),&width,&height)) throw std::runtime_error(SDL_GetError());
        const auto logical_width = std::clamp(600.0f*static_cast<float>(width)/static_cast<float>(std::max(1,height)),440.0f,1800.0f);
        Canvas canvas(logical_width);
        paint(canvas);
        if (options_.frames && drawn+1 == options_.frames && !options_.capture.empty()) {
            renderer_.capture_next(options_.capture);
        }
        const auto items = world_->extract(std::clamp(static_cast<float>(accumulator_/Game::timestep),0.0f,1.0f));
        if (renderer_.draw(items,{eye_,focus_,0.88f,settings_.exposure,settings_.antialias},canvas.vertices())) {
            ++drawn;
            if (options_.frames && drawn == options_.frames) break;
        } else SDL_Delay(10);
        if (options_.frames && now > stop_at) throw std::runtime_error("Window diagnostic timed out");
    }
    const auto& report = renderer_.report();
    std::cout << "frames=" << drawn << " surface=" << report.width << 'x' << report.height
              << " submitted=" << report.submitted << " culled=" << report.culled
              << " gpu_ms=" << report.gpu_ms << " validation_errors=" << renderer_.validation_errors() << '\n';
    return (!options_.frames || drawn == options_.frames) && renderer_.validation_errors() == 0 ? 0 : 1;
}

}

int run_game(int argc,char** argv) {
    if (argc==2 && std::string_view(argv[1]) == "--help") {
        std::cout << "lantern-reach [--assets DIR] [--shaders DIR] [--profile DIR] [--validate]\n"
                     "              [--size WIDTHxHEIGHT] [--no-audio] [--level 1..3]\n"
                     "              [--frames N [--capture FILE.ppm]]\n";
        return 0;
    }
    const auto options = parse(argc,argv);
    return Application(options).run();
}

}

int main(int argc,char** argv) {
    try {
        return ember::native::run_game(argc,argv);
    } catch (const std::exception& error) {
        std::cerr << "The Lantern Reach: " << error.what() << '\n';
        return 1;
    }
}

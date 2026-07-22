#include "audio.hpp"
#include "emberfall/net.hpp"
#include "emberfall/replay.hpp"

#include <charconv>
#include <iostream>
#include <limits>

namespace ember::native {
namespace {

enum class Screen { title, playing, pause, settings, next, finished };

struct Options {
    std::filesystem::path assets, shaders, profile, capture, replay, record;
    unsigned frames{}, level{};
    int width{1100}, height{700};
    bool validation{}, audio{true}, hosting{}, replay_fast{}, ui_check{};
    std::string address, key;
    std::uint16_t port{};
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
        else if (arg == "--replay") options.replay = value();
        else if (arg == "--replay-fast") options.replay_fast = true;
        else if (arg == "--record") options.record = value();
        else if (arg == "--key") options.key = value();
        else if (arg == "--host" || arg == "--join") {
            if (options.port) throw std::runtime_error("Specify exactly one network role");
            options.hosting = arg == "--host";
            if (!options.hosting) options.address = value();
            const auto port = positive(value());
            if (port > 65535) throw std::runtime_error("UDP port outside bounds");
            options.port = static_cast<std::uint16_t>(port);
        }
        else if (arg == "--validate") options.validation = true;
        else if (arg == "--no-audio") options.audio = false;
        else if (arg == "--ui-check") options.ui_check = true;
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
    if (options.ui_check) {
        if (options.profile.empty() || options.level || options.port || !options.replay.empty() ||
            !options.record.empty() || std::filesystem::exists(options.profile/"settings.save") ||
            std::filesystem::exists(options.profile/"campaign.save") || (options.frames && options.frames<44)) {
            throw std::runtime_error("UI check requires a fresh explicit profile and at least 44 frames");
        }
        if (!options.frames) options.frames = 50;
    }
    if (options.level > Campaign::level_count || options.frames > 10000 || (!options.capture.empty() && !options.frames && options.replay.empty())) {
        throw std::runtime_error("Level or capture frame outside bounds");
    }
    if ((!options.replay.empty() && (options.port || options.level || options.frames || !options.record.empty())) ||
        (options.replay_fast && options.replay.empty()) || (!options.record.empty() && options.port && !options.hosting)) {
        throw std::runtime_error("Replay arguments conflict with level, frame or network options");
    }
    if (!options.record.empty() && std::filesystem::exists(options.record)) {
        throw std::runtime_error("Recording destination already exists");
    }
    if (options.port && (options.key.size() < 4 || options.key.size() > 64)) {
        throw std::runtime_error("Network play requires a shared key of 4 to 64 characters");
    }
    if (!options.port && !options.key.empty()) throw std::runtime_error("A key requires a network role");
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
    void save_tape();
    void finish_level();
    Command companion();
    void trigger(GameEvent event);
    void poll_network(std::uint64_t now);
    void client_input(Command command);
    void accept(net::ServerFrame frame,std::uint64_t now);
    void load(Progress progress);
    void checkpoint();
    void persist();
    void paint(Canvas& canvas) const;
    void check_ui(unsigned frame);
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
    bool quit_{}, follow_{};
    std::deque<Vec3> route_;
    std::uint64_t route_tick_{};
    std::optional<Replay> script_, recording_;
    std::size_t script_cursor_{};
    bool recording_done_{}, script_done_{};
    std::unique_ptr<net::Session> network_;
    std::deque<net::InputFrame> history_;
    std::uint32_t epoch_{}, net_order_{}, net_jump_{}, net_use_{}, applied_input_{};
    unsigned network_level_{}, first_ui_width_{};
    std::uint64_t last_broadcast_{}, sample_time_{}, last_server_tick_{};
    Vec3 remote_before_{}, remote_after_{};
};

Application::Application(const Options& options)
    : options_(options), platform_("The Lantern Reach",options.width,options.height,options.audio),
      renderer_(platform_.window(),options.shaders,options.validation), campaign_(options.assets,true) {
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
    if (options.port) {
        const auto key = fingerprint(options.key);
        if (options.hosting) {
            network_ = net::Session::host(options.port,key,campaign_.revision());
            if (progress_.complete) progress_ = {};
            load(options.level ? Progress{options.level-1,0,false} : progress_);
        } else {
            network_ = net::Session::join(options.address,options.port,key,campaign_.revision());
            load({});
            note_ = "Waiting for host";
            note_time_ = 5;
        }
        std::cout << "network=" << (options.hosting ? "host" : "client") << " port=" << network_->port() << '\n';
    } else if (!options.replay.empty()) {
        script_ = read_replay(options.replay);
        if (script_->revision != campaign_.revision()) throw std::runtime_error("Replay campaign revision mismatch");
        load(script_->start);
    } else if (options.level) load({options.level-1,0,false});
    std::cout << renderer_.adapter() << " audio=" << (audio_.enabled() ? "ready" : "unavailable") << '\n';
}

void Application::load(Progress progress) {
    if (progress.complete) {
        screen_ = Screen::finished;
        return;
    }
    if (recording_ && !recording_->ticks.empty()) save_tape();
    world_ = campaign_.load(progress);
    progress_ = progress;
    if (!options_.record.empty() && !recording_done_) recording_ = Replay{campaign_.revision(),progress,{}};
    screen_ = Screen::playing;
    selected_ = network_ && !options_.hosting ? 1U : 0U;
    if (network_ && options_.hosting) {
        if (++epoch_ == 0) ++epoch_;
        network_level_ = progress.level;
        network_->set_epoch(epoch_);
        applied_input_ = 0;
        last_broadcast_ = 0;
    }
    focus_ = world_->player(selected_).body.feet + Vec3{0,0,-1.2f};
    eye_ = focus_ + Vec3{1.4f,7.2f,8.2f};
    accumulator_ = 0;
    pending_ = {};
    follow_ = false;
    route_.clear();
    route_tick_ = 0;
}

void Application::checkpoint() {
    if (script_ || (network_ && !options_.hosting)) return;
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
    if (script_) return;
    if (input.fullscreen) {
        settings_.fullscreen = !settings_.fullscreen;
        platform_.set_fullscreen(settings_.fullscreen);
        persist();
    }
    if (screen_ == Screen::playing) {
        if (input.pause || (input.lost_focus && (!options_.frames || options_.ui_check))) {
            screen_ = Screen::pause;
            cursor_ = 0;
            pending_ = {};
            if (network_ && !options_.hosting) client_input({});
            return;
        }
        if (input.select && !network_) {
            selected_ ^= 1;
            pending_ = {};
            follow_ = false;
            route_.clear();
            route_tick_ = 0;
        }
        if (input.follow && !network_) {
            follow_ = !follow_;
            route_.clear();
            route_tick_ = 0;
            note_ = follow_ ? "Companion following" : "Companion holding position";
            note_time_ = 2;
        }
        if (input.restart && (!network_ || options_.hosting)) {
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
        if (input.enter && (!network_ || options_.hosting)) load(progress_);
        if (input.pause && (!network_ || options_.hosting)) { screen_ = Screen::title; cursor_ = 0; }
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
        else if (input.enter && cursor_ == 1 && (!network_ || options_.hosting)) load({progress_.level,world_->checkpoint(),false});
        else if (input.enter && cursor_ == 2) { previous_ = screen_; screen_ = Screen::settings; cursor_ = 0; }
        else if (input.enter && cursor_ == 3) { if (network_) quit_ = true; else screen_ = Screen::title; cursor_ = 0; }
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

void Application::client_input(Command command) {
    if (!network_->connected() || !epoch_ || world_->completed()) return;
    if (command.jump) ++net_jump_;
    if (command.interact) ++net_use_;
    if (++net_order_ == 0) ++net_order_;
    const net::InputFrame packet{epoch_,net_order_,net_jump_,net_use_,command};
    network_->input(packet);
    history_.push_back(packet);
    if (history_.size() > 48) history_.pop_front();
    world_->tick({Command{},command});
}

void Application::accept(net::ServerFrame frame,std::uint64_t now) {
    const bool transitioned = frame.epoch != epoch_;
    if (epoch_ && transitioned && !net::newer(frame.epoch,epoch_)) return;
    if (!transitioned && frame.simulation.ticks < last_server_tick_) return;
    if (transitioned) {
        load({frame.level,0,false});
        epoch_ = frame.epoch;
        net_order_ = net_jump_ = net_use_ = 0;
        history_.clear();
    }
    const auto previous_checkpoint = progress_.checkpoint;
    world_->apply_snapshot(frame.simulation);
    last_server_tick_ = frame.simulation.ticks;
    remote_before_ = transitioned ? frame.simulation.players[0].body.feet : remote_after_;
    remote_after_ = frame.simulation.players[0].body.feet;
    sample_time_ = now;
    while (!history_.empty() && frame.acknowledged && !net::newer(history_.front().order,frame.acknowledged)) {
        history_.pop_front();
    }
    for (const auto& packet : history_) world_->tick({Command{},packet.command});
    if (!frame.simulation.completed) progress_.checkpoint = frame.simulation.checkpoint;
    if (!frame.simulation.completed && frame.simulation.checkpoint && frame.simulation.checkpoint != previous_checkpoint) {
        trigger({Signal::checkpoint,frame.simulation.checkpoint});
    }
    if (frame.simulation.completed && screen_ != Screen::next && screen_ != Screen::finished) {
        audio_.cue(Signal::exit);
        const bool final = frame.level+1 == Campaign::level_count;
        progress_ = {final ? frame.level : frame.level+1,0,final};
        screen_ = final ? Screen::finished : Screen::next;
    }
}

void Application::poll_network(std::uint64_t now) {
    if (!network_) return;
    network_->poll(now);
    if (options_.hosting) {
        if (epoch_ && now-last_broadcast_ >= 50) {
            network_->broadcast({epoch_,applied_input_,network_level_,world_->snapshot()});
            last_broadcast_ = now;
        }
    } else if (auto frame = network_->receive()) {
        try { accept(std::move(*frame),now); }
        catch (const std::exception& error) {
            note_ = error.what();
            note_time_ = 4;
        }
    }
}

void Application::save_tape() {
    if (!recording_ || recording_->ticks.empty()) return;
    write_replay(options_.record,*recording_);
    recording_.reset();
    recording_done_ = true;
}

void Application::finish_level() {
    const bool final = progress_.level+1 == Campaign::level_count;
    progress_ = {final ? progress_.level : progress_.level+1,0,final};
    checkpoint();
    save_tape();
    screen_ = final ? Screen::finished : Screen::next;
}

Command Application::companion() {
    const auto goal = world_->player(selected_).body.feet;
    const auto current = world_->player(selected_^1).body.feet;
    if (!follow_ || length(goal-current)<1.25f) {
        route_.clear();
        return {};
    }
    if (world_->ticks()>=route_tick_) {
        const auto planned = world_->route(current,goal);
        route_.assign(planned.waypoints.begin(),planned.waypoints.end());
        route_tick_ = world_->ticks()+8;
    }
    while (!route_.empty()) {
        auto delta = route_.front()-current;
        delta.y = 0;
        if (length(delta)>0.12f) return Command{unit(delta)*std::min(0.86f,length(delta)*2.4f)};
        route_.pop_front();
    }
    return {};
}

void Application::simulate(double elapsed) {
    note_time_ = std::max(0.0,note_time_-elapsed);
    if (screen_ != Screen::playing) return;
    const auto cap = static_cast<double>(Game::timestep)*5;
    accumulator_ = std::min(accumulator_+elapsed,cap);
    if (script_ && options_.replay_fast) accumulator_ = 4*static_cast<double>(Game::timestep);
    while (accumulator_ >= Game::timestep && screen_ == Screen::playing) {
        std::array<Command,2> pair{};
        pair[selected_] = pending_;
        if (script_) pair = script_->ticks.at(script_cursor_).commands;
        else if (!network_) pair[selected_^1] = companion();
        pending_.jump = pending_.interact = false;
        if (network_ && !options_.hosting) {
            client_input(pair[1]);
            accumulator_ -= Game::timestep;
            continue;
        }
        if (network_) {
            pair[1] = network_->remote_command();
            applied_input_ = network_->last_input();
        }
        world_->tick(pair);
        accumulator_ -= Game::timestep;
        if (script_) {
            const auto expected = script_->ticks[script_cursor_++].state;
            if (world_->state_hash() != expected) throw std::runtime_error("Replay divergence at tick " + std::to_string(script_cursor_));
            script_done_ = script_cursor_ == script_->ticks.size();
            if (world_->completed() && !script_done_) throw std::runtime_error("Replay contains steps after completion");
        }
        if (recording_) {
            recording_->ticks.push_back({pair,world_->state_hash()});
            if (recording_->ticks.size() == Replay::tick_limit) save_tape();
        }
        for (const auto event : world_->events()) trigger(event);
        if (world_->completed()) finish_level();
        if (script_done_) break;
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
        const auto player_label = selected_ ? "GLEAM" : "EMBER";
        const auto status = std::string(player_label) + (network_ ?
            (network_->connected() ? "   /   CO-OP CONNECTED" : "   /   WAITING FOR PEER") :
            (follow_ ? "   /   COMPANION FOLLOWING" : selected_ ? "   /   EMBER HOLDING" : "   /   GLEAM HOLDING"));
        canvas.text(23,548,1.7f,status,selected_ ? cyan : amber);
        canvas.wrapped(23,572,1.43f,network_ ? "WASD MOVE   SPACE JUMP   E USE   CTRL CROUCH   ESC PAUSE" :
                       "WASD MOVE   SPACE JUMP   E USE   TAB SWITCH   F FOLLOW   ESC PAUSE",muted);
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
        canvas.wrapped(32,156,1.6f,"Two explorers. One dark observatory.",muted,panel_width-36);
        constexpr std::array<std::string_view,4> rows{"Continue","New expedition","Settings","Quit"};
        menu(canvas,215,rows);
        canvas.wrapped(32,418,1.5f,"Walk and jump between platforms. Switch explorers to leave one on a relay.",white,panel_width-36);
        canvas.wrapped(32,471,1.35f,"ARROWS OR D-PAD    ENTER OR A",muted);
        if (note_time_ > 0) canvas.wrapped(32,500,1.35f,note_.substr(0,120),amber,panel_width-36);
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
            campaign_.scene(progress_.level).title,cyan,panel_width-36);
        canvas.wrapped(32,245,1.5f,finished ? "ENTER TO RETURN TO TITLE" : network_ && !options_.hosting ? "WAITING FOR HOST" : "ENTER TO CONTINUE",white);
    }
}

void Application::check_ui(unsigned frame) {
    const auto key = [](SDL_Scancode code) {
        SDL_Event input{};
        input.type = SDL_EVENT_KEY_DOWN;
        input.key.scancode = code;
        if (!SDL_PushEvent(&input)) throw std::runtime_error("Could not enqueue native key event");
    };
    const auto focus = [](SDL_EventType type) {
        SDL_Event input{};
        input.type = type;
        if (!SDL_PushEvent(&input)) throw std::runtime_error("Could not enqueue native focus event");
    };
    const auto require = [](bool condition,const char* detail) {
        if (!condition) throw std::runtime_error(std::string("UI diagnostic: ")+detail);
    };
    switch (frame) {
    case 1: focus(SDL_EVENT_WINDOW_FOCUS_GAINED); break;
    case 3: case 4: key(SDL_SCANCODE_DOWN); break;
    case 5: key(SDL_SCANCODE_RETURN); break;
    case 6: require(screen_==Screen::settings,"settings screen not active"); break;
    case 8: key(SDL_SCANCODE_LEFT); break;
    case 9:
        require(std::abs(settings_.volume-0.55f)<0.0001f &&
                read_settings(options_.profile/"settings.save")==settings_,"volume did not persist");
        break;
    case 10: key(SDL_SCANCODE_ESCAPE); break;
    case 12: key(SDL_SCANCODE_RETURN); break;
    case 14: require(screen_==Screen::playing && selected_==0,"continue did not start the room"); break;
    case 16: key(SDL_SCANCODE_TAB); break;
    case 17: require(selected_==1,"companion switch did not apply"); break;
    case 18: focus(SDL_EVENT_WINDOW_FOCUS_LOST); break;
    case 19: require(screen_==Screen::pause,"focus loss did not pause"); break;
    case 20: focus(SDL_EVENT_WINDOW_FOCUS_GAINED); break;
    case 22: key(SDL_SCANCODE_RETURN); break;
    case 23: require(screen_==Screen::playing,"resume failed"); break;
    case 26: key(SDL_SCANCODE_ESCAPE); break;
    case 28: key(SDL_SCANCODE_DOWN); break;
    case 29: key(SDL_SCANCODE_RETURN); break;
    case 30: require(screen_==Screen::playing && selected_==0,"restart did not reset selection"); break;
    case 34:
        first_ui_width_ = renderer_.report().width;
        require(SDL_SetWindowSize(platform_.window(),540,380),"window resize failed");
        renderer_.resize();
        break;
    case 41:
        require(first_ui_width_!=0 && renderer_.report().width!=first_ui_width_,"drawable did not resize");
        std::cout << "ui_check=settings,save,keyboard,focus,pause,restart,resize\n";
        break;
    default: break;
    }
}

int Application::run() {
    auto previous = SDL_GetTicksNS();
    unsigned drawn = 0, checked_frame = std::numeric_limits<unsigned>::max();
    const auto stop_at = previous+60'000'000'000ULL;
    while (!quit_) {
        const auto now = SDL_GetTicksNS();
        const auto elapsed = std::clamp(static_cast<double>(now-previous)*1e-9,0.0,0.1);
        previous = now;
        if (options_.ui_check && checked_frame!=drawn) {
            check_ui(drawn);
            checked_frame = drawn;
        }
        handle(platform_.input());
        if (quit_) break;
        simulate(elapsed);
        poll_network(now/1'000'000);
        audio_.update(settings_.volume);
        int width = 0, height = 0;
        if (!SDL_GetWindowSize(platform_.window(),&width,&height)) throw std::runtime_error(SDL_GetError());
        const auto logical_width = std::clamp(600.0f*static_cast<float>(width)/static_cast<float>(std::max(1,height)),440.0f,1800.0f);
        Canvas canvas(logical_width);
        paint(canvas);
        if (((options_.frames && drawn+1 == options_.frames) || script_done_) && !options_.capture.empty()) {
            renderer_.capture_next(options_.capture);
        }
        std::optional<Vec3> remote;
        if (network_ && !options_.hosting && epoch_) {
            const float weight = std::clamp(static_cast<float>(now/1'000'000-sample_time_)/50,0.0f,1.0f);
            remote = lerp(remote_before_,remote_after_,weight);
        }
        const auto alpha = std::clamp(static_cast<float>(accumulator_/Game::timestep),0.0f,1.0f);
        const auto items = world_->extract(alpha,remote);
        const auto animation = world_->skin(alpha,remote);
        if (renderer_.draw(items,{eye_,focus_,0.88f,settings_.exposure,settings_.antialias},canvas.vertices(),animation,world_->imported_meshes())) {
            ++drawn;
            if ((options_.frames && drawn == options_.frames) || script_done_) break;
        } else SDL_Delay(10);
        if (options_.frames && now > stop_at) throw std::runtime_error("Window diagnostic timed out");
    }
    save_tape();
    if (script_) {
        std::cout << "replay_ticks=" << script_cursor_ << " final_hash=" << world_->state_hash()
                  << " completed=" << world_->completed() << '\n';
    }
    const auto& report = renderer_.report();
    std::cout << "frames=" << drawn << " surface=" << report.width << 'x' << report.height
              << " submitted=" << report.submitted << " culled=" << report.culled
              << " mesh_triangles=" << report.imported_triangles << " gpu_ms=" << report.gpu_ms << " validation_errors=" << renderer_.validation_errors() << '\n';
    if (network_) {
        const auto& net = network_->stats();
        std::cout << "packets_sent=" << net.sent << " packets_received=" << net.received
                  << " connected=" << network_->connected() << '\n';
    }
    return (!options_.frames || drawn == options_.frames) && (!script_ || script_done_) && renderer_.validation_errors() == 0 ? 0 : 1;
}

}

int run_game(int argc,char** argv) {
    if (argc==2 && std::string_view(argv[1]) == "--help") {
        std::cout << "lantern-reach [--assets DIR] [--shaders DIR] [--profile DIR] [--validate]\n"
                     "              [--size WIDTHxHEIGHT] [--no-audio] [--level 1..3]\n"
                     "              [--frames N [--capture FILE.ppm]]\n"
                     "              [--host PORT | --join IPV4 PORT] --key SHARED_KEY\n"
                     "              [--record FILE | --replay FILE [--replay-fast]]\n"
                     "              [--ui-check --profile FRESH_DIRECTORY]\n";
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

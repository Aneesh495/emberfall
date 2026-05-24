#include "platform.hpp"
#include "emberfall/editor.hpp"

#include <charconv>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace ember::native {
namespace {

enum class Field : unsigned {
    name, kind, position_x, position_y, position_z, size_x, size_y, size_z,
    yaw, red, green, blue, link, parent, destination_x, destination_y, destination_z, parameter
};

constexpr std::array<std::string_view,18> field_names{
    "Name","Kind","Position X","Position Y","Position Z","Size X","Size Y","Size Z","Yaw radians",
    "Tint R","Tint G","Tint B","Link ID","Parent ID","Target X","Target Y","Target Z","Parameter"
};
constexpr Color slate{0.022f,0.038f,0.066f,0.97f};
constexpr Color cyan{0.16f,0.79f,0.72f};
constexpr Color ivory{0.84f,0.89f,0.98f};
constexpr Color quiet{0.46f,0.56f,0.68f};
constexpr Color orange{1.0f,0.54f,0.2f};

template<class T>
T number(std::string_view value) {
    T parsed{};
    const auto result = std::from_chars(value.data(),value.data()+value.size(),parsed);
    if (result.ec != std::errc{} || result.ptr != value.data()+value.size()) {
        throw std::runtime_error("Invalid number");
    }
    return parsed;
}

float& real_field(Node& n,Field field) {
    switch (field) {
    case Field::position_x: return n.position.x;
    case Field::position_y: return n.position.y;
    case Field::position_z: return n.position.z;
    case Field::size_x: return n.size.x;
    case Field::size_y: return n.size.y;
    case Field::size_z: return n.size.z;
    case Field::yaw: return n.yaw;
    case Field::red: return n.color.x;
    case Field::green: return n.color.y;
    case Field::blue: return n.color.z;
    case Field::destination_x: return n.destination.x;
    case Field::destination_y: return n.destination.y;
    case Field::destination_z: return n.destination.z;
    case Field::parameter: return n.parameter;
    default: throw std::logic_error("Inspector field is not a real");
    }
}

std::string value(Node n, Field field) {
    switch (field) {
    case Field::name: return n.name;
    case Field::kind: return std::string(kind_name(n.kind));
    case Field::link: return std::to_string(n.link);
    case Field::parent: return std::to_string(n.parent);
    default:
        std::ostringstream out;
        out << std::setprecision(7) << real_field(n,field);
        return out.str();
    }
}

void assign(Node& node,Field field,const std::string& text) {
    switch (field) {
    case Field::name: node.name = text; break;
    case Field::kind: {
        const auto space = text.find(' ');
        const auto name = std::string_view(text).substr(0,space);
        for (unsigned i=0; i<=static_cast<unsigned>(Kind::crate); ++i) {
            if (kind_name(static_cast<Kind>(i)) != name) continue;
            node.kind = static_cast<Kind>(i);
            node.link = space == std::string::npos ? 0 : number<SceneId>(std::string_view(text).substr(space+1));
            return;
        }
        throw std::runtime_error("Unknown node kind");
    }
    case Field::link: node.link = number<SceneId>(text); break;
    case Field::parent: node.parent = number<SceneId>(text); break;
    default: real_field(node,field) = number<float>(text); break;
    }
}

struct Options {
    std::filesystem::path file, shaders, capture;
    unsigned frames{};
    bool validation{};
};

struct Drag {
    float x{}, y{};
    unsigned axis{};
    float delta{};
};

class Editor {
public:
    explicit Editor(Options options);
    int run();

private:
    void event(const SDL_Event& e);
    void key(const SDL_KeyboardEvent& e);
    void click(const SDL_MouseButtonEvent& event);
    void edit(const std::vector<Edit>& transaction);
    void field_input();
    void select(SceneId id,bool keep);
    void refresh_assets();
    void add_prefab();
    void make_prefab();
    std::vector<RenderItem> extract() const;
    void paint(Canvas& canvas) const;
    std::vector<SceneId> visible_nodes() const;
    View view() const;

    Options options_;
    Platform platform_;
    Renderer renderer_;
    EditorFile file_;
    std::unique_ptr<Game> play_;
    Vec3 center_{0,0,0};
    float zoom_{1};
    std::vector<SceneId> selected_;
    std::vector<std::filesystem::path> assets_;
    std::size_t selected_asset_{}, scroll_{};
    Field field_{};
    std::optional<Drag> drag_;
    std::string text_, status_{"Ready"};
    float logical_width_{1020};
    int width_{1190}, height_{700};
    bool typing_{}, snapping_{true}, paused_{}, step_{}, close_{}, guard_{};
};

Editor::Editor(Options options)
    : options_(std::move(options)), platform_("Emberfall Scene Editor",1190,700,false),
      renderer_(platform_.window(),options_.shaders,options_.validation), file_(options_.file) {
    refresh_assets();
    if (file_.recovered()) status_ = "Recovered unsaved changes";
}

View Editor::view() const {
    if (play_) {
        const auto focus = play_->player(0).body.feet + Vec3{0,0,-1};
        return {focus+Vec3{1.4f,7.2f,8.2f},focus};
    }
    return {center_+Vec3{6,16,20}*zoom_,center_};
}

void Editor::refresh_assets() {
    assets_.clear();
    const auto folder = options_.file.has_parent_path() ? options_.file.parent_path() : ".";
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (assets_.size() == 128) break;
        if (entry.is_regular_file() && entry.path().extension() == ".prefab") assets_.push_back(entry.path());
    }
    std::ranges::sort(assets_);
    selected_asset_ = 0;
}

void Editor::edit(const std::vector<Edit>& transaction) {
    file_.document().apply(transaction);
    file_.write_recovery();
    status_ = "Transaction applied";
}

void Editor::select(SceneId id,bool keep) {
    if (!keep) selected_.clear();
    if (!id) return;
    const auto found = std::ranges::find(selected_,id);
    if (found == selected_.end()) {
        if (selected_.size() < 64) selected_.push_back(id);
    } else selected_.erase(found);
}

std::vector<SceneId> Editor::visible_nodes() const {
    std::vector<SceneId> out;
    std::size_t i = 0;
    for (const auto& [id,n] : file_.document().scene().nodes) {
        (void)n;
        if (i++ < scroll_) continue;
        if (out.size() == 22) break;
        out.push_back(id);
    }
    return out;
}

void Editor::field_input() {
    if (selected_.empty()) return;
    const auto& node = file_.document().scene().nodes.at(selected_.back());
    if (!typing_) {
        text_.clear();
        SDL_StartTextInput(platform_.window());
        typing_ = true;
        status_ = field_ == Field::kind ? "Enter kind and optional link ID. Example: door 10" :
            "Enter replacement for " + std::string(field_names[static_cast<unsigned>(field_)]);
    } else {
        SDL_StopTextInput(platform_.window());
        typing_ = false;
        if (text_.empty()) return;
        auto modified = node;
        assign(modified,field_,text_);
        edit({{node,std::move(modified)}});
    }
}

void Editor::make_prefab() {
    if (selected_.empty()) throw std::runtime_error("Select a group before saving a prefab");
    const auto& scene = file_.document().scene();
    Scene prefab{"Prefab selection","Reusable editor selection",{}};
    for (const auto id : selected_) prefab.nodes.emplace(id,scene.nodes.at(id));
    const auto destination = (options_.file.has_parent_path() ? options_.file.parent_path() : ".")/"selection.prefab";
    write_atomic(destination,write_scene(prefab));
    refresh_assets();
    status_ = "Saved selection prefab";
}

void Editor::add_prefab() {
    if (assets_.empty()) throw std::runtime_error("No prefab in this directory");
    const auto prefab = load_scene(assets_.at(selected_asset_));
    const auto added = instantiate_prefab(prefab,file_.document().allocate_id(),{1.5f,0,0});
    edit(added);
    selected_.clear();
    for (const auto& item : added) selected_.push_back(item.after->id);
}

void Editor::key(const SDL_KeyboardEvent& e) {
    if (e.repeat) return;
    const bool command = e.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI);
    const bool shift = e.mod & SDL_KMOD_SHIFT;
    if (typing_) {
        if (e.scancode == SDL_SCANCODE_RETURN) field_input();
        if (e.scancode == SDL_SCANCODE_BACKSPACE && !text_.empty()) text_.pop_back();
        if (e.scancode == SDL_SCANCODE_ESCAPE) {
            SDL_StopTextInput(platform_.window());
            typing_ = false;
            status_ = "Field input cancelled";
        }
        return;
    }
    if (e.scancode == SDL_SCANCODE_F5) {
        if (play_) play_.reset();
        else play_ = file_.document().play();
        paused_ = false;
        status_ = play_ ? "Play world running" : "Returned to edit world";
        return;
    }
    if (play_) {
        if (e.scancode == SDL_SCANCODE_F6) paused_ = !paused_;
        if (e.scancode == SDL_SCANCODE_F7 && paused_) step_ = true;
        return;
    }
    auto& doc = file_.document();
    if (command && e.scancode == SDL_SCANCODE_S) {
        file_.save();
        status_ = "Scene saved";
    } else if (command && e.scancode == SDL_SCANCODE_Z) {
        if (shift ? doc.redo() : doc.undo()) {
            std::erase_if(selected_,[&](SceneId id) { return !doc.scene().nodes.contains(id); });
            file_.write_recovery();
            status_ = shift ? "Redone" : "Undone";
        }
    } else if (command && e.scancode == SDL_SCANCODE_P) make_prefab();
    else if (e.scancode == SDL_SCANCODE_I) add_prefab();
    else if (e.scancode == SDL_SCANCODE_V && !assets_.empty()) selected_asset_ = (selected_asset_+1)%assets_.size();
    else if (e.scancode == SDL_SCANCODE_G) snapping_ = !snapping_;
    else if (e.scancode == SDL_SCANCODE_RETURN) field_input();
    else if (e.scancode == SDL_SCANCODE_INSERT || e.scancode == SDL_SCANCODE_N) {
        const auto id = doc.allocate_id();
        Node node{.id=id,.name="New block",.position=center_+Vec3{0,1,0},.color={0.26f,0.53f,0.65f}};
        edit({{{},node}});
        select(id,false);
    } else if (e.scancode == SDL_SCANCODE_DELETE || e.scancode == SDL_SCANCODE_BACKSPACE) {
        std::vector<Edit> removed;
        for (const auto id : selected_) removed.push_back({doc.scene().nodes.at(id),{}});
        edit(removed);
        selected_.clear();
    } else if (e.scancode == SDL_SCANCODE_UP) {
        field_ = static_cast<Field>((static_cast<unsigned>(field_)+17)%18);
    } else if (e.scancode == SDL_SCANCODE_DOWN) {
        field_ = static_cast<Field>((static_cast<unsigned>(field_)+1)%18);
    } else if (e.scancode == SDL_SCANCODE_LEFT || e.scancode == SDL_SCANCODE_RIGHT) {
        if (selected_.empty()) return;
        const auto& n = doc.scene().nodes.at(selected_.back());
        auto modified = n;
        if (field_ == Field::name || field_ == Field::kind || field_ == Field::link || field_ == Field::parent) return;
        real_field(modified,field_) += (e.scancode == SDL_SCANCODE_LEFT ? -1.0f : 1.0f)*(snapping_ ? 0.25f : 0.05f);
        edit({{n,std::move(modified)}});
    } else if (e.scancode == SDL_SCANCODE_ESCAPE) {
        selected_.clear();
    }
}

void Editor::click(const SDL_MouseButtonEvent& event) {
    if (play_ || typing_ || event.button != SDL_BUTTON_LEFT) return;
    const float x = event.x/static_cast<float>(std::max(1,width_))*logical_width_;
    const float y = event.y/static_cast<float>(std::max(1,height_))*600;
    if (y < 45 || y >= 547) return;
    if (x < 207) {
        const auto nodes = visible_nodes();
        if (y >= 74 && y < 470) {
            const auto row = static_cast<std::size_t>((y-74)/18);
            if (row < nodes.size()) select(nodes[row],(SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
        } else if (y >= 492 && !assets_.empty()) {
            selected_asset_ = (selected_asset_+1)%assets_.size();
        }
        return;
    }
    if (x > logical_width_-220) {
        if (y >= 74 && y < 488) {
            field_ = static_cast<Field>(static_cast<unsigned>((y-74)/23));
            if (event.clicks >= 2) field_input();
        }
        return;
    }
    const auto camera = view();
    const auto forward = unit(camera.target-camera.eye);
    const auto right = unit(cross(forward,{0,1,0}));
    const auto up = cross(right,forward);
    const float aspect = static_cast<float>(width_)/static_cast<float>(std::max(1,height_));
    const float tangent = std::tan(camera.fov*0.5f);
    const auto ray = forward + right*((x/logical_width_*2-1)*aspect*tangent) + up*((1-y/300)*tangent);
    const auto hit = pick_node(file_.document().scene(),camera.eye,ray);
    const bool shift = SDL_GetModState() & SDL_KMOD_SHIFT;
    if (hit && !shift && std::ranges::find(selected_,hit) != selected_.end()) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        unsigned axis = keys[SDL_SCANCODE_Y] ? 1U : keys[SDL_SCANCODE_Z] ? 2U : 0U;
        drag_ = Drag{event.x,event.y,axis,0};
    } else select(hit,shift);
}

void Editor::event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_KEY_DOWN) key(e.key);
    if (e.type == SDL_EVENT_TEXT_INPUT && typing_) {
        if (text_.size()+std::string_view(e.text.text).size() <= 80) text_ += e.text.text;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) click(e.button);
    if (e.type == SDL_EVENT_MOUSE_MOTION && drag_) {
        float amount = drag_->axis == 0 ? e.motion.x-drag_->x : drag_->y-e.motion.y;
        amount *= 0.027f*zoom_;
        drag_->delta = snapping_ ? std::round(amount*4)*0.25f : amount;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT && drag_) {
        Vec3 delta;
        if (drag_->axis == 0) delta.x = drag_->delta;
        if (drag_->axis == 1) delta.y = drag_->delta;
        if (drag_->axis == 2) delta.z = drag_->delta;
        drag_.reset();
        if (delta != Vec3{}) edit(shift_nodes(file_.document().scene(),selected_,delta));
    }
    if (e.type == SDL_EVENT_MOUSE_WHEEL && !play_) {
        if (e.wheel.mouse_x/static_cast<float>(std::max(1,width_))*logical_width_ < 207) {
            const auto limit = file_.document().scene().nodes.size();
            if (e.wheel.y < 0 && scroll_+1 < limit) ++scroll_;
            if (e.wheel.y > 0 && scroll_) --scroll_;
        } else zoom_ = std::clamp(zoom_*std::exp(-e.wheel.y*0.13f),0.35f,2.5f);
    }
}

std::vector<RenderItem> Editor::extract() const {
    const auto& scene = file_.document().scene();
    if (play_) return play_->extract(1);
    if (selected_.empty()) return extract_edit_scene(scene);
    Scene preview = scene;
    if (drag_) {
        Vec3 delta;
        if (drag_->axis == 0) delta.x = drag_->delta;
        if (drag_->axis == 1) delta.y = drag_->delta;
        if (drag_->axis == 2) delta.z = drag_->delta;
        for (const auto& item : shift_nodes(scene,selected_,delta)) preview.nodes.at(item.after->id) = *item.after;
    }
    const auto matrices = resolve_transforms(preview);
    std::vector<RenderItem> out;
    out.reserve(scene.nodes.size()+3);
    for (const auto& [id,node] : preview.nodes) {
        const bool active = std::ranges::find(selected_,id) != selected_.end();
        out.push_back({matrices.at(id),node.color,active ? 0.8f : node.kind == Kind::decor ? node.parameter : 0});
    }
    const auto origin = transform_point(matrices.at(selected_.back()),{});
    constexpr std::array axes{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
    constexpr std::array colors{Vec3{0.95f,0.12f,0.09f},Vec3{0.15f,0.88f,0.28f},Vec3{0.14f,0.33f,0.95f}};
    for (std::size_t i=0; i<axes.size(); ++i) {
        out.push_back({transform_matrix(origin+axes[i]*0.7f,{},{0.045f+axes[i].x*1.4f,
                       0.045f+axes[i].y*1.4f,0.045f+axes[i].z*1.4f}),colors[i],2.0f});
    }
    return out;
}

void Editor::paint(Canvas& canvas) const {
    const auto& doc = file_.document();
    canvas.box(0,0,logical_width_,44,slate);
    canvas.text(15,15,2.2f,"EMBERFALL   /   SCENE WORKSHOP",ivory);
    canvas.text(logical_width_-204,16,1.55f,play_ ? (paused_ ? "PLAY WORLD PAUSED" : "PLAY WORLD ACTIVE") :
                doc.dirty() ? "UNSAVED SCENE" : "EDIT WORLD",play_ ? orange : cyan);
    if (!play_) {
        canvas.box(0,45,207,502,slate);
        canvas.box(logical_width_-220,45,220,502,slate);
        canvas.text(12,56,1.45f,"HIERARCHY",cyan);
        float y = 75;
        for (const auto id : visible_nodes()) {
            const auto& node = doc.scene().nodes.at(id);
            const bool active = std::ranges::find(selected_,id) != selected_.end();
            if (active) canvas.box(6,y-2,194,17,{0.12f,0.29f,0.31f});
            const auto row = std::to_string(id)+" "+(node.parent ? "> " : "")+node.name;
            canvas.text(11,y+1,1.25f,row.substr(0,24),active ? ivory : quiet);
            y += 18;
        }
        canvas.box(8,475,190,1,quiet);
        canvas.text(12,483,1.4f,"PREFABS   /   V CYCLE",cyan);
        const auto asset = assets_.empty() ? std::string("No local prefabs") : assets_.at(selected_asset_).stem().string();
        canvas.text(12,511,1.2f,asset.substr(0,23),quiet);
        const float start = logical_width_-213;
        canvas.text(start,56,1.45f,"INSPECTOR",cyan);
        if (!selected_.empty()) {
            const auto& node = doc.scene().nodes.at(selected_.back());
            for (unsigned i=0; i<field_names.size(); ++i) {
                const float top = 74+static_cast<float>(i)*23;
                const bool active = i == static_cast<unsigned>(field_);
                if (active) canvas.box(start-2,top,210,22,{0.12f,0.29f,0.31f});
                canvas.text(start+3,top+7,1.18f,field_names[i],active ? ivory : quiet);
                const auto v = active && typing_ ? text_ : value(node,static_cast<Field>(i));
                canvas.text(start+94,top+7,1.18f,v.substr(0,15),typing_ && active ? orange : ivory);
            }
        } else canvas.text(start,79,1.25f,"SELECT A NODE",quiet);
        canvas.text(start,511,1.25f,snapping_ ? "SNAP ON   /   G TO TOGGLE" : "SNAP OFF  /   G TO TOGGLE",quiet);
    }
    canvas.box(0,547,logical_width_,53,slate);
    canvas.wrapped(13,554,1.3f,status_.substr(0,125),orange);
    const auto hint = play_ ? "F5 STOP   F6 PAUSE   F7 STEP   WASD MOVE" :
        "N ADD   CTRL-S SAVE   CTRL-Z UNDO   F5 PLAY   SHIFT SELECT   DRAG X/Y/Z   ENTER EDIT";
    canvas.wrapped(13,577,1.14f,hint,quiet);
}

int Editor::run() {
    unsigned drawn = 0;
    auto previous = SDL_GetTicksNS();
    double time = 0;
    bool jump = false, interact = false;
    const auto deadline = previous+30'000'000'000ULL;
    while (!close_) {
        const auto now = SDL_GetTicksNS();
        const auto elapsed = std::min(static_cast<double>(now-previous)*1e-9,0.1);
        previous = now;
        if (!SDL_GetWindowSize(platform_.window(),&width_,&height_)) throw std::runtime_error(SDL_GetError());
        logical_width_ = std::max(700.0f,600.0f*static_cast<float>(width_)/static_cast<float>(std::max(1,height_)));
        const auto input = platform_.input([&](const auto& e) {
            try { event(e); }
            catch (const std::exception& error) { status_ = error.what(); }
        });
        if (input.resized) renderer_.resize();
        if (input.quit) {
            if (!file_.document().dirty() || guard_) break;
            guard_ = true;
            status_ = "Unsaved scene. Close again to keep recovery journal";
        }
        if (play_) {
            jump = jump || input.jump;
            interact = interact || input.interact;
            if (!paused_) time = std::min(time+elapsed,5.0*Game::timestep);
            if (step_) { time = Game::timestep; step_ = false; }
            while (time >= Game::timestep) {
                play_->tick({Command{input.movement,jump,interact,input.crouch},{}});
                jump = interact = false;
                time -= Game::timestep;
            }
        } else {
            center_ += input.movement*static_cast<float>(elapsed*6);
            jump = interact = false;
            time = 0;
        }
        Canvas canvas(logical_width_);
        paint(canvas);
        if (options_.frames && drawn+1==options_.frames && !options_.capture.empty()) renderer_.capture_next(options_.capture);
        if (renderer_.draw(extract(),view(),canvas.vertices())) {
            if (++drawn == options_.frames) break;
        } else SDL_Delay(10);
        if (options_.frames && now > deadline) throw std::runtime_error("Editor diagnostic timed out");
    }
    std::cout << "editor_frames=" << drawn << " validation_errors=" << renderer_.validation_errors() << '\n';
    return renderer_.validation_errors() == 0 && (!options_.frames || drawn == options_.frames) ? 0 : 1;
}

}

int run_editor(int argc,char** argv) {
    if (argc<2 || std::string_view(argv[1]) == "--help") {
        std::cout << "emberfall-editor SCENE [--validate] [--frames N [--capture FILE.ppm]]\n"
                     "emberfall-editor --new SCENE\n"
                     "emberfall-editor --translate SCENE ID DX DY DZ\n";
        return argc < 2 ? 1 : 0;
    }
    const std::string_view action = argv[1];
    if (action == "--translate" && argc == 7) {
        EditorFile file(argv[2]);
        const std::array id{number<SceneId>(argv[3])};
        const Vec3 delta{number<float>(argv[4]),number<float>(argv[5]),number<float>(argv[6])};
        auto& document = file.document();
        document.apply(shift_nodes(document.scene(),id,delta));
        file.write_recovery();
        file.save();
        std::cout << "Revised node " << id[0] << " in " << argv[2] << '\n';
        return 0;
    }
    if (action == "--new" && argc == 3) {
        if (std::filesystem::exists(argv[2])) throw std::runtime_error("Scene already exists");
        Scene empty{"Untitled room","Set an objective",{}};
        empty.nodes.emplace(1,Node{.id=1,.kind=Kind::solid,.name="Floor",.position={0,-0.25f,0},.size={12,0.5f,12}});
        write_atomic(argv[2],write_scene(empty));
        return 0;
    }
    Options options;
    options.file = argv[1];
    options.shaders = executable_directory()/"shaders";
    for (int i=2; i<argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--validate") options.validation = true;
        else if (argument == "--frames" && i+1<argc) options.frames = number<unsigned>(argv[++i]);
        else if (argument == "--capture" && i+1<argc) options.capture = argv[++i];
        else throw std::runtime_error("Unknown editor option");
    }
    if (options.frames > 10000 || (!options.frames && !options.capture.empty())) throw std::runtime_error("Invalid capture limit");
    return Editor(std::move(options)).run();
}

}

int main(int argc,char** argv) {
    try { return ember::native::run_editor(argc,argv); }
    catch (const std::exception& error) {
        std::cerr << "Emberfall editor: " << error.what() << '\n';
        return 1;
    }
}

#include "inspector.hpp"
#include "emberfall/prefab.hpp"

#include <iomanip>
#include <ranges>

#include <sstream>

namespace ember::native {
namespace {

constexpr Color panel{0.022f,0.038f,0.066f,0.97f};
constexpr Color label{0.16f,0.79f,0.72f};
constexpr Color text{0.84f,0.89f,0.98f};
constexpr Color muted{0.56f,0.67f,0.76f};
constexpr Color amber{1,0.52f,0.14f};

float card(Canvas& canvas,std::string_view title,float height) {
    const auto x = (canvas.width()-278)*0.5f;
    canvas.box(x,53,278,height,panel);
    canvas.box(x,53,278,2,label);
    canvas.text(x+10,64,1.50f,title,label);
    return x+10;
}

std::string precision(double value,int digits = 2) {
    std::ostringstream result;
    result << std::fixed << std::setprecision(digits) << value;
    return result.str();
}

std::string flags(std::string_view name,std::uint64_t first,std::uint64_t second) {
    std::ostringstream result;
    result << name << ' ' << std::hex << std::uppercase << first << " > " << second;
    return result.str();
}

std::string vector_text(Vec3 p) {
    return precision(p.x,1)+", "+precision(p.y,1)+", "+precision(p.z,1);
}

std::optional<std::array<float,2>> project(const Mat4& camera,Vec3 world,float width) {
    const auto clip = transform_point(camera,world);
    const float w = camera(3,0)*world.x+camera(3,1)*world.y+camera(3,2)*world.z+camera(3,3);
    if (w<=0.1f || clip.z<0 || clip.z>w) return {};
    return std::array{(clip.x/w*0.5f+0.5f)*width,(clip.y/w*0.5f+0.5f)*600};
}

void box_edges(Canvas& canvas,const Mat4& camera,const Box& box,Color color) {
    std::array<std::optional<std::array<float,2>>,8> points;
    for (unsigned i=0; i<8; ++i) {
        const Vec3 p{i&1U ? box.maximum.x : box.minimum.x,i&2U ? box.maximum.y : box.minimum.y,
                     i&4U ? box.maximum.z : box.minimum.z};
        points[i] = project(camera,p,canvas.width());
    }
    for (unsigned i=0; i<8; ++i) for (const unsigned axis : {1U,2U,4U}) {
        if (i&axis || !points[i] || !points[i|axis]) continue;
        const auto [x0,y0] = *points[i];
        const auto [x1,y1] = *points[i|axis];
        canvas.line(x0,y0,x1,y1,1.25f,color);
    }
}

}

AssetLinks load_asset_links(const std::filesystem::path& scene) {
    AssetLinks result;
    const auto catalog = scene.parent_path()/"ember.catalog";
    if (!std::filesystem::is_regular_file(catalog) || std::filesystem::is_symlink(catalog)) return result;
    std::istringstream input(read_bounded(catalog,32768));
    std::string row;
    if (!std::getline(input,row) || row!="ember-content 1") return result;
    std::multimap<std::string,std::string> edges;
    while (std::getline(input,row)) {
        std::istringstream fields(row);
        std::string kind, source, value;
        if (!(fields >> kind >> source >> value)) continue;
        if (kind=="mesh") {
            const auto cooked = std::filesystem::path(source).stem().string()+".mesh";
            result.models.try_emplace(cooked,AssetLinks::Model{source,value,{}});
        } else if (kind=="mesh-buffer" || kind=="mesh-image") {
            edges.emplace(source,value);
        } else if (kind=="place" && source==scene.filename()) {
            if (!std::string_view(value).contains('/') && !std::string_view(value).contains('\\')) {
                result.placed_prefabs.try_emplace(value);
            }
        }
    }
    for (auto& [name, model] : result.models) {
        (void)name;
        const auto [first,last] = edges.equal_range(model.source);
        for (auto edge=first; edge!=last; ++edge) model.inputs.push_back(edge->second);
    }
    for (auto& [prefab,sources] : result.placed_prefabs) {
        try {
            sources = load_prefab(scene.parent_path()/prefab).sources;
        } catch (const std::exception&) {
            sources.clear();
        }
    }
    return result;
}

void paint_frame_inspector(Canvas& canvas,const Renderer& renderer,std::size_t pass_index) {
    const auto& frame = renderer.report();
    const auto& graph = renderer.graph_report();
    const auto index = graph.pass_count ? pass_index%graph.pass_count : 0;
    const auto& pass = graph.passes[index];
    const auto x = card(canvas,"FRAME PROFILER",240+45*static_cast<float>(pass.image_count));
    canvas.text(x,85,1.18f,"GPU "+precision(frame.gpu_ms)+" MS   FRAME "+std::to_string(frame.timed_serial),text);
    canvas.text(x,100,1.18f,"ALLOCATED "+precision(static_cast<double>(frame.owned_bytes)/(1024*1024),1)+" MIB",text);
    canvas.text(x,115,1.18f,std::to_string(frame.triangles)+" TRIS   "+std::to_string(frame.draw_calls)+" DRAWS",text);
    canvas.text(x,130,1.18f,std::to_string(frame.lights)+" LIGHTS   "+std::to_string(frame.tile_light_entries)+" TILE LINKS",text);
    canvas.text(x,145,1.18f,std::to_string(frame.texture_layers)+" MAPS   "+std::to_string(frame.uploaded_texture_layers)+" UPLOADED",text);
    constexpr std::array phase{"PREPASS / SHADOW","OPAQUE HDR","TEMPORAL","EFFECTS","DISPLAY / CAPTURE"};
    for (std::size_t i=0; i<phase.size(); ++i) {
        const auto y = 168+static_cast<float>(i)*16;
        const auto time = frame.pass_gpu_ms[i];
        canvas.text(x,y,1.05f,phase[i],muted);
        canvas.box(x+121,y+1,94,5,{0.10f,0.16f,0.22f,1});
        const auto ratio = std::clamp(time/std::max(frame.gpu_ms,0.001),0.0,1.0);
        canvas.box(x+121,y+1,94*static_cast<float>(ratio),5,label);
        canvas.text(x+224,y,1.05f,precision(time),text);
    }
    if (!graph.pass_count) return;
    canvas.text(x,252,1.18f,std::to_string(index+1)+"/"+std::to_string(graph.pass_count)+" "+std::string(pass.name),label);
    canvas.text(x,266,1.03f,"PAGE UP / DOWN SELECTS PASS",muted);
    for (std::size_t i=0; i<pass.image_count; ++i) {
        const auto& image = pass.images[i];
        const float y = 285+45*static_cast<float>(i);
        canvas.text(x,y,1.12f,std::string(image.resource)+"  "+std::to_string(image.levels)+"M "+std::to_string(image.layers)+"L",text);
        canvas.text(x,y+11,1.02f,std::string(image.previous_layout)+" > "+std::string(image.layout),muted);
        canvas.text(x,y+22,1.02f,flags("STAGE",image.source_stage,image.destination_stage),muted);
        canvas.text(x,y+33,1.02f,flags("ACCESS",image.source_access,image.destination_access),muted);
    }
}

std::size_t paint_collider_overlay(Canvas& canvas,const Game& game,const View& view,float aspect) {
    const auto camera = perspective(view.fov,aspect,0.1f,view.far_plane)*look_at(view.eye,view.target,{0,1,0});
    constexpr std::size_t limit = 128;
    const auto visible = game.colliders().first(std::min(limit,game.colliders().size()));
    for (const auto& collider : visible) {
        const bool support = game.player(0).body.ground==collider.id || game.player(1).body.ground==collider.id;
        box_edges(canvas,camera,collider.bounds,support ? amber : Color{label.r,label.g,label.b,0.5f});
    }
    return visible.size();
}

void paint_physics_inspector(Canvas& canvas,const Game* game,std::size_t drawn) {
    const auto x = card(canvas,"COLLISION SPACE",149);
    if (!game) {
        canvas.text(x,87,1.2f,"F5 STARTS THE PLAY WORLD",text);
        return;
    }
    canvas.text(x,85,1.15f,std::to_string(drawn)+"/"+std::to_string(game->colliders().size())+" AABB WIREFRAMES",text);
    canvas.text(x,100,1.15f,"SIMULATION TICK "+std::to_string(game->ticks()),text);
    for (std::size_t i=0; i<2; ++i) {
        const auto& body = game->player(i).body;
        const auto y = 119+static_cast<float>(i)*35;
        canvas.text(x,y,1.15f,"PLAYER "+std::to_string(i+1)+"   SUPPORT "+std::to_string(body.ground),amber);
        canvas.text(x,y+13,1.06f,"FEET "+vector_text(body.feet)+"    V "+vector_text(body.velocity),muted);
    }
}

void paint_asset_inspector(Canvas& canvas,const AssetLinks& links,std::string_view name,const MeshAsset* mesh) {
    const auto x = card(canvas,"ASSET DEPENDENCIES",254);
    const auto model = links.models.find(name);
    canvas.text(x,86,1.18f,name.empty() ? "NO MODEL SELECTED" : name,text);
    float y = 102;
    if (mesh) {
        canvas.text(x,y,1.12f,std::to_string(mesh->vertices)+" VERTICES   "+std::to_string(mesh->textures.size())+" LAYERS",muted);
        y += 15;
    }
    if (model!=links.models.end()) {
        canvas.text(x,y,1.08f,model->second.source,muted);
        canvas.text(x,y+13,1.04f,"GUID "+model->second.guid,muted);
        y += 30;
        for (const auto& dependency : model->second.inputs | std::views::take(5)) {
            canvas.text(x+8,y,1.08f,dependency,text);
            y += 14;
        }
    } else if (!name.empty()) {
        canvas.text(x,y,1.05f,"SOURCE CATALOG ENTRY UNAVAILABLE",muted);
        y += 15;
    }
    canvas.text(x,y+6,1.15f,"COOKED SCENE PREFABS",label);
    y += 23;
    for (const auto& [prefab,sources] : links.placed_prefabs | std::views::take(2)) {
        canvas.text(x+8,y,1.08f,prefab,text);
        y += 12;
        if (sources.size()>1) {
            canvas.text(x+14,y,1.00f,"BASE "+sources[1],muted);
            y += 12;
        } else if (sources.empty()) {
            canvas.text(x+14,y,1.00f,"UNRESOLVED",amber);
            y += 12;
        }
    }
}

}

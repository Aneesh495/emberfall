#include "platform.hpp"

namespace ember::native {
namespace {

using Glyph = std::array<unsigned char,7>;

constexpr std::array<Glyph,26> letters{{
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,14}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,27,17}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
}};
constexpr std::array<Glyph,10> digits{{
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,6,8,16,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12}
}};

Glyph glyph(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c-'a'+'A');
    if (c >= 'A' && c <= 'Z') return letters[static_cast<std::size_t>(c-'A')];
    if (c >= '0' && c <= '9') return digits[static_cast<std::size_t>(c-'0')];
    switch (c) {
    case '.': return {0,0,0,0,0,0,4};
    case ',': return {0,0,0,0,0,4,8};
    case '-': return {0,0,0,14,0,0,0};
    case '/': return {1,1,2,4,8,16,16};
    case ':': return {0,4,4,0,4,4,0};
    case '!': return {4,4,4,4,4,0,4};
    case '?': return {14,17,1,2,4,0,4};
    case '+': return {0,4,4,31,4,4,0};
    case '[': return {12,8,8,8,8,8,12};
    case ']': return {6,2,2,2,2,2,6};
    case '>': return {16,8,4,2,4,8,16};
    case '<': return {1,2,4,8,4,2,1};
    default: return {};
    }
}

}

void Canvas::box(float x,float y,float w,float h,Color c) {
    const auto vertex = [&](float u,float v) { return OverlayVertex{u/width_,v/600,{c.r,c.g,c.b,c.a}}; };
    const auto a = vertex(x,y), b = vertex(x+w,y), d = vertex(x,y+h), e = vertex(x+w,y+h);
    vertices_.insert(vertices_.end(),{a,b,e,a,e,d});
}

void Canvas::line(float x0,float y0,float x1,float y1,float thickness,Color c) {
    const auto length = std::hypot(x1-x0,y1-y0);
    if (length < 0.01f) return;
    const auto dx = (y0-y1)*thickness/(2*length), dy = (x1-x0)*thickness/(2*length);
    const auto vertex = [&](float x,float y) {
        return OverlayVertex{x/width_,y/600,{c.r,c.g,c.b,c.a}};
    };
    const auto a = vertex(x0+dx,y0+dy), b = vertex(x0-dx,y0-dy);
    const auto d = vertex(x1+dx,y1+dy), e = vertex(x1-dx,y1-dy);
    vertices_.insert(vertices_.end(),{a,b,e,a,e,d});
}

void Canvas::text(float x,float y,float size,std::string_view content,Color c) {
    for (char ch : content) {
        const auto rows = glyph(ch);
        for (std::size_t row=0; row<rows.size(); ++row) {
            for (unsigned col=0; col<5; ++col) {
                if ((static_cast<unsigned>(rows[row]) & (16U >> col)) == 0) continue;
                box(x+static_cast<float>(col)*size,y+static_cast<float>(row)*size,size,size,c);
            }
        }
        x += size*6;
    }
}

void Canvas::wrapped(float x,float y,float size,std::string_view content,Color c,float max_width) {
    const float right = max_width > 0 ? std::min(x+max_width,width_-x) : width_-x;
    float cursor = x;
    while (!content.empty()) {
        const auto at = content.find(' ');
        const auto word = content.substr(0,at);
        const float advance = size*6*static_cast<float>(word.size());
        if (cursor != x && cursor+advance > right) {
            cursor = x;
            y += size*10;
        }
        text(cursor,y,size,word,c);
        cursor += advance + size*6;
        if (at == std::string_view::npos) return;
        content.remove_prefix(at+1);
    }
}

}

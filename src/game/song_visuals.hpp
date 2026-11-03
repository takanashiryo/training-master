#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace training::song_visuals {
using Ptr=std::uintptr_t;
using Visible=bool(__fastcall*)(Ptr);
using SetVisible=void(__fastcall*)(Ptr,bool);
using PostRender=bool(__fastcall*)(Ptr);
struct Layout {
    Ptr ex_bg{},autopara{},post{},layer{},priority{},ex_draw{},post_render{},target{};
    Visible visible{};
    SetVisible set_visible{};
    Ptr live2d{};
    SetVisible set_live2d_visible{};
    PostRender present{};
    explicit operator bool() const { return ex_bg && autopara && post && layer && priority && ex_draw && post_render && target && visible && set_visible && live2d && set_live2d_visible && present; }
};
// Restore extra sprite/Live2D visibility before native updates destroy owners.
// Base backgrounds and native capture/composition continue running.
class Controller {
public:
    void configure(Layout layout) { restore(); layout_=layout; }
    void restore();
    bool apply(Ptr world,bool hide);
    bool hides_view(Ptr view) const;
    bool hides_effect(Ptr effect) const;
    bool render(PostRender original,Ptr effect);
    const std::string& error() const { return error_; }
private:
    struct Effect { Ptr owner,processor; std::vector<Ptr> bypass; };
    struct Live2D { Ptr component; unsigned char visible; };
    struct Model { Ptr model,table; unsigned char visible; };
    struct Sprite { Ptr owner; unsigned handle; bool visible; };
    void hide_sprite(Ptr layer);
    void hide_live2d(Ptr component);
    void collect(Ptr view);
    Layout layout_;
    std::vector<Ptr> views_;
    std::vector<Effect> effects_;
    std::vector<Live2D> live2d_;
    std::vector<Model> models_;
    std::vector<Sprite> sprites_;
    const char* stage_{"world views"};
    std::string error_;
};
}

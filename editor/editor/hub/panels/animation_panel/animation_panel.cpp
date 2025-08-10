#include "animation_panel.h"
#include "../panels_defs.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/gizmo.h>

namespace unravel
{
namespace
{

} // namespace

void animation_panel::draw_menubar(rtti::context& ctx)
{
    if(ImGui::BeginMenuBar())
    {
        ImGui::EndMenuBar();
    }
}

animation_panel::animation_panel(imgui_panels* parent)
{
}

void animation_panel::init(rtti::context& ctx)
{
    struct CustomNode : public ImFlow::BaseNode
    {
        explicit CustomNode()
        {
            setTitle("Custom");
            setStyle(ImFlow::NodeStyle::brown());
            addIN<int>("in<int>", "int", 0, ImFlow::ConnectionFilter::SameType(), ImFlow::PinStyle::red());

            addOUT<int>("out<int>", "int", ImFlow::PinStyle::blue())
                ->behaviour(
                    [this]()
                    {
                        return 0;
                    });
        }
    };

    struct Custom2Node : public ImFlow::BaseNode
    {
        explicit Custom2Node()
        {
            setTitle("Custom2");
            setStyle(ImFlow::NodeStyle::brown());
            addIN<int>("in<int>", "int", 0, ImFlow::ConnectionFilter::SameType(), ImFlow::PinStyle::red());
            addIN<float>("in<float>", "float", 0, ImFlow::ConnectionFilter::SameType(), ImFlow::PinStyle::red());

            addOUT<int>("out<int>", "int", ImFlow::PinStyle::blue())
                ->behaviour(
                    [this]()
                    {
                        return 0;
                    });
            addOUT<float>("out<float>", "float", ImFlow::PinStyle::blue())
                ->behaviour(
                    [this]()
                    {
                        return 0.0f;
                    });
        }

        void draw() override
        {
            ImGui::PushFont(ImGui::Font::Bold);
            ImGui::Text("%s", "some text here");

            ImGui::PopFont();
        }
    };

    auto callback = [this]()
    {
        /* omitted */

        float size = 200.0f;
        static ImGuiTextFilter filter_;

        ImGui::DrawFilterWithHint(filter_, ICON_MDI_SELECT_SEARCH " Search...", size);
        ImGui::DrawItemActivityOutline();

        ImGui::Separator();
        ImGui::BeginChild("COMPONENT_MENU_CONTEXT", ImVec2(ImGui::GetContentRegionAvail().x, size));

        struct node_factory
        {
            std::string name;
            std::function<void()> factory;
        };

        std::vector<node_factory> nodes{
            {"Custom",
             [this]()
             {
                 flow_.placeNode<CustomNode>();
             }},
            {"Custom2",
             [this]()
             {
                 flow_.placeNode<Custom2Node>();
             }},
        };

        for(const auto& factory : nodes)
        {
            if(!filter_.PassFilter(factory.name.c_str()))
                continue;

            if(ImGui::Selectable(factory.name.c_str()))
            {
                factory.factory();

                ImGui::CloseCurrentPopup();
            }
        };

        ImGui::EndChild();
    };

    flow_.rightClickPopUpContent(
        [this, callback](ImFlow::BaseNode* node)
        {
            callback();
        });

    flow_.droppedLinkPopUpContent(
        [this, callback](ImFlow::Pin* dragged)
        {
            callback();
        });

    flow_.addNode<CustomNode>({});
    //show(true);
}

void animation_panel::deinit(rtti::context& ctx)
{
}

void animation_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(show_request_)
    {
        show_request_ = false;
        show_ = true;
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * 0.5f, ImGuiCond_Once);
    }
    if(show_)
    {
        if(ImGui::Begin(name, &show_, ImGuiWindowFlags_MenuBar))
        {
            // ImGui::WindowTimeBlock block(ImGui::GetFont(ImGui::Font::Mono));

            draw_ui(ctx);
        }
        ImGui::End();
    }

}

void animation_panel::show(bool s)
{
    show_request_ = s;
}

void animation_panel::draw_ui(rtti::context& ctx)
{
    flow_.update();
}

} // namespace unravel

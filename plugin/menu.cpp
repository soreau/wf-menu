/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Scott Moreau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <wayfire/core.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/txn/transaction-manager.hpp>

#include <linux/input-event-codes.h>

#include "wf-cube-control-signal.hpp"
#include "wf-menu-server-protocol.h"
#include "wf-menu-actions.hpp"

extern "C"
{
#include <wlr/types/wlr_xdg_shell.h>
}

static wl_resource *menu_resource;
static wayfire_view menu_view, parent_view;
std::vector<std::pair<uint32_t, std::string>> menu_items;
static void bind_menu(wl_client * client, void *data, uint32_t, uint32_t id);

namespace wf
{
namespace menu
{
static const std::string barrel_roll_transformer_name = "barrel-roll-transformer";
static const std::string workspace_switch_transformer_name = "workspace-switch-transformer";
using namespace wf::animation;

class barrel_roll_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
};

class workspace_switch_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
};


static const char *shadow_vert_source =
    R"(
#version 100

precision highp float;

attribute highp vec2 position;

uniform mat4 matrix;

void main() {
    gl_Position = matrix * vec4(position, 0.0, 1.0);
}
)";

static const char *shadow_frag_source =
    R"(
#version 100

precision highp float;

vec4 shadow_color = vec4(0.0, 0.0, 0.0, 0.2);
uniform float corner_radius;
uniform vec2 size;

void main()
{
    float d;
    float shadow_radius = 10.0;
    vec4 c = shadow_color;
    vec4 m = vec4(0.0);
    vec4 s;
    vec2 pos = gl_FragCoord.xy;
    float diffuse = 2.0 / shadow_radius;

    // top
    if (pos.x > corner_radius * 2.0 && pos.x < size.x - corner_radius * 2.0 && pos.y < corner_radius * 2.0)
    {
        d = distance(vec2(pos.x, corner_radius), pos);
        gl_FragColor = mix(c, m, 1.0 - exp(-pow(d * diffuse, 2.0)));
        return;
    }
    // right
    if (pos.x > size.x - corner_radius * 2.0 && pos.y > corner_radius * 2.0 && pos.y < size.y - corner_radius * 2.0)
    {
        d = distance(vec2(size.x - corner_radius, pos.y), pos);
        gl_FragColor = mix(c, m, 1.0 - exp(-pow(d * diffuse, 2.0)));
        return;
    }
    // bottom
    if (pos.x > (corner_radius * 2.0) && pos.x < size.x - (corner_radius * 2.0) && pos.y > size.y - corner_radius * 2.0)
    {
        d = distance(vec2(pos.x, size.y - corner_radius), pos);
        gl_FragColor = mix(c, m, 1.0 - exp(-pow(d * diffuse, 2.0)));
        return;
    }
    // left
    if (pos.x < corner_radius * 2.0 && pos.y > corner_radius * 2.0 && pos.y < size.y - corner_radius * 2.0)
    {
        d = distance(vec2(corner_radius, pos.y), pos);
        gl_FragColor = mix(c, m, 1.0 - exp(-pow(d * diffuse, 2.0)));
        return;
    }
    // top left corner
    if (pos.x < corner_radius * 2.0 && pos.y < corner_radius * 2.0)
    {
        d = distance(vec2(corner_radius * 2.0), pos) - corner_radius;
        s = mix(c, m, 1.0 - exp(-pow(d * diffuse, 2.0)));
        d = distance(vec2(corner_radius * 2.0), pos) - corner_radius;
        gl_FragColor = mix(c, s, clamp(d, 0.0, 1.0));
        return;
    }
    // top right corner
    if (pos.x > size.x - (corner_radius * 2.0) && pos.y < (corner_radius * 2.0))
    {
        d = distance(vec2(size.x - corner_radius * 2.0, corner_radius * 2.0), pos) - corner_radius;
        s = mix(c, m, 1.0 - exp(-pow(d * diffuse, 2.0)));
        d = distance(vec2(size.x - corner_radius * 2.0, corner_radius * 2.0), pos) - corner_radius;
        gl_FragColor = mix(c, s, clamp(d, 0.0, 1.0));
        return;
    }
    // bottom right corner
    if (pos.x > size.x - (corner_radius * 2.0) && pos.y > size.y - (corner_radius * 2.0))
    {
        d = distance(vec2(size.x - corner_radius * 2.0, float(size.y - (corner_radius * 2.0))), pos) - corner_radius;
        s = mix(c, m, 1.0 - exp(-pow(d * diffuse, 2.0)));
        d = distance(vec2(size.x - corner_radius * 2.0, float(size.y - (corner_radius * 2.0))), pos) - corner_radius;
        gl_FragColor = mix(c, s, clamp(d, 0.0, 1.0));
        return;
    }
    // bottom left corner
    if (pos.x < (corner_radius * 2.0) && pos.y > size.y - (corner_radius * 2.0))
    {
        d = distance(vec2(corner_radius * 2.0, float(size.y - (corner_radius * 2.0))), pos) - corner_radius;
        s = mix(c, m, 1.0 - exp(-pow(d * diffuse, 2.0)));
        d = distance(vec2(corner_radius * 2.0, float(size.y - (corner_radius * 2.0))), pos) - corner_radius;
        gl_FragColor = mix(c, s, clamp(d, 0.0, 1.0));
        return;
    }

    discard;
}
)";

class simple_shadow_node_t : public wf::scene::node_t
{
    wf::option_wrapper_t<int> corner_radius{"wf-menu/corner_radius"};
    wayfire_toplevel_view view;
    OpenGL::program_t program;

  public:

    simple_shadow_node_t(wayfire_toplevel_view view) : wf::scene::node_t(false)
    {
        this->view = view;
        program.set_simple(OpenGL::compile_program(
            shadow_vert_source, shadow_frag_source));
    }

    ~simple_shadow_node_t()
    {}

    class shadow_render_instance_t : public wf::scene::render_instance_t
    {
        simple_shadow_node_t *self;
        wf::scene::damage_callback push_damage;

        wf::signal::connection_t<wf::scene::node_damage_signal> on_surface_damage =
            [=] (wf::scene::node_damage_signal *data)
        {
            push_damage(data->region);
        };

      public:
        shadow_render_instance_t(simple_shadow_node_t *self, wf::scene::damage_callback push_damage)
        {
            this->self = self;
            this->push_damage = push_damage;
            self->connect(&on_surface_damage);
        }

        void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage) override
        {
            instructions.push_back(wf::scene::render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage & self->get_bounding_box(),
                    });
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            static const float vertex_data[] = {
                -1.0f, -1.0f,
                1.0f, -1.0f,
                1.0f, 1.0f,
                -1.0f, 1.0f
            };
            auto vg = self->view->get_geometry();
            data.pass->custom_gles_subpass(data.target, [&]
            {
                self->program.use(wf::TEXTURE_TYPE_RGBA);
                self->program.uniformMatrix4f("matrix", wf::gles::output_transform(data.target));
                self->program.attrib_pointer("position", 2, 0, vertex_data);
                self->program.uniform2f("size", vg.width, vg.height);
                self->program.uniform1f("corner_radius", std::max(int(self->corner_radius), 10));
                gles::for_each_scissor_rect(data.target, data.damage, [&]
                {
                    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
                });
                self->program.deactivate();
            });
        }
    };

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *output = nullptr) override
    {
        instances.push_back(std::make_unique<shadow_render_instance_t>(this, push_damage));
    }

    wf::geometry_t get_bounding_box() override
    {
        auto vg = view->get_geometry();
        vg.x = vg.y = -std::max(int(corner_radius), 10);
        return vg;
    }
};

class wf_menu : public wf::plugin_interface_t
{
    wf::option_wrapper_t<int> corner_radius{"wf-menu/corner_radius"};
    bool barrel_roll_clockwise;
    barrel_roll_animation_t barrel_roll_progression;
    workspace_switch_animation_t workspace_switch_progression;
    wf::geometry_t workspace_from_geometry, workspace_to_geometry;
    wayfire_view barrel_roll_view, workspace_switch_view;
    wl_global *menu_global;
    cube_spin_animation_t cube_spin_animation{wf::create_option<int>(1500)};
    cube_spin_state cube_state = CUBE_SPIN_DISABLED;
    bool cube_spin_hook_set    = false;

  public:
    void init() override
    {
        menu_view   = parent_view = nullptr;
        menu_global = wl_global_create(wf::get_core().display,
            &wf_menu_manager_interface,
            1, this, bind_menu);

        wf::get_core().connect(&on_button_event);
        wf::get_core().connect(&on_view_mapped);
        wf::get_core().connect(&on_show_menu);

        barrel_roll_progression = barrel_roll_animation_t(wf::create_option<int>(1500));
        workspace_switch_progression = workspace_switch_animation_t(wf::create_option<int>(1500));

        prepare_desktop_menu();
    }

    void prepare_desktop_menu()
    {
        int i = 1;
        auto output = wf::get_core().seat->get_active_output();
        if (!output)
        {
            return;
        }

        auto workspace_grid_size = output->wset()->get_workspace_grid_size();

        menu_items.clear();

        menu_items.push_back({i++, DESKTOP_CHANGE_BG});
        menu_items.push_back({i++, DESKTOP_LAUNCH_WCM});
        menu_items.push_back({0, DESKTOP_MOVE_TO_WORKSPACE});
        for (int y = 1; y <= workspace_grid_size.height; y++)
        {
            for (int x = 1; x <= workspace_grid_size.width; x++)
            {
                menu_items.push_back({i++,
                    "Workspace [" + std::to_string(x) + "," + std::to_string(y) + "]"});
            }
        }

        menu_items.push_back({0, ""});
        menu_items.push_back({i++, DESKTOP_CUBE_SPIN});
        menu_items.push_back({i++, DESKTOP_DO_A_BARREL_ROLL});
    }

    void prepare_wm_menu()
    {
        int i = 1;
        auto output = wf::get_core().seat->get_active_output();
        if (!output)
        {
            return;
        }

        auto workspace_grid_size = output->wset()->get_workspace_grid_size();

        menu_items.clear();

        menu_items.push_back({i++, WM_MINIMIZE});
        if (parent_view && wf::toplevel_cast(parent_view) &&
            wf::toplevel_cast(parent_view)->pending_tiled_edges())
        {
            menu_items.push_back({i++, WM_UNMAXIMIZE});
        } else
        {
            menu_items.push_back({i++, WM_MAXIMIZE});
        }

        menu_items.push_back({i++, WM_CLOSE});
        menu_items.push_back({0, WM_SEND_TO_WORKSPACE});
        for (int y = 1; y <= workspace_grid_size.height; y++)
        {
            for (int x = 1; x <= workspace_grid_size.width; x++)
            {
                menu_items.push_back({i++,
                    "Workspace [" + std::to_string(x) + "," + std::to_string(y) + "]"});
            }
        }

        menu_items.push_back({0, ""});
        menu_items.push_back({i++, WM_DO_A_BARREL_ROLL});
    }

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        auto toplevel = wf::toplevel_cast(ev->view);

        if (!toplevel)
        {
            return;
        }

        if (toplevel->get_app_id() == "wf-menu")
        {
            menu_view = ev->view;

            if (!menu_view)
            {
                return;
            }

            for (auto v : wf::get_core().get_all_views())
            {
                if (v && v->get_wlr_surface() && (v->role == wf::VIEW_ROLE_UNMANAGED))
                {
                    if (wlr_xdg_popup_try_from_wlr_surface(v->get_wlr_surface()))
                    {
                        if (menu_view)
                        {
                            wf::view_unmapped_signal unmap_signal;
                            unmap_signal.view = menu_view;
                            wf::get_core().emit(&unmap_signal);
                            wf::scene::set_node_enabled(menu_view->get_transformed_node(), false);
                            wf::scene::set_node_enabled(menu_view->get_root_node(), false);
                            menu_view->close();
                            menu_view = nullptr;
                            return;
                        }
                    }
                }
            }

            auto output = menu_view->get_output();
            if (parent_view)
            {
                /* Move top left of menu to mouse cursor position */
                if (output)
                {
                    auto pos = output->get_cursor_position();
                    toplevel->move(pos.x, pos.y);
                }
            } else
            {
                /* Center the window on the output */
                if (output)
                {
                    auto og = output->get_relative_geometry();
                    auto vg = toplevel->get_geometry();
                    toplevel->move((og.width - vg.width) / 2.0, (og.height - vg.height) / 2.0);
                    for (auto v : wf::get_core().get_all_views())
                    {
                        if (v->get_app_id() == "gtk4-layer-shell")
                        {
                            parent_view = v;
                            break;
                        }
                    }
                }
            }

            /* Skip taskbar */
            wf::view_unmapped_signal unmap_signal;
            unmap_signal.view = menu_view;
            wf::get_core().emit(&unmap_signal);
            /* Connect signals */
            menu_view->connect(&on_view_unmapped);
            /* Add padding to surface for shadow rendering */
            auto& pending = toplevel->toplevel()->pending();
            auto r = double(std::max(int(corner_radius), 10));
            pending.margins  = {r, r, r, r};
            pending.geometry = wf::expand_geometry_by_margins(pending.geometry, pending.margins);
            wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
            /* Move to very topmost layer */
            if (output)
            {
                wf::scene::readd_front(output->node_for_layer(wf::scene::layer::LOCK),
                    menu_view->get_root_node());
            }

            /* Add drop shadow */
            auto shadow = std::make_shared<simple_shadow_node_t>(toplevel);
            wf::scene::add_back(menu_view->get_surface_root_node(), shadow);

            /* Populate menu items list */
            (parent_view &&
                parent_view->role !=
                wf::VIEW_ROLE_DESKTOP_ENVIRONMENT) ? prepare_wm_menu() : prepare_desktop_menu();
            /* Send the item list data to the menu client */
            send_menu_items();
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped = [=] (wf::view_unmapped_signal *ev)
    {
        if (ev->view == parent_view)
        {
            if (menu_view)
            {
                menu_view->close();
                menu_view = nullptr;
                on_view_unmapped.disconnect();
            }

            parent_view = nullptr;
            return;
        }

        if (ev->view == menu_view)
        {
            menu_view   = nullptr;
            parent_view = nullptr;
            on_view_unmapped.disconnect();
        }
    };

    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_button_event>> on_button_event =
        [=] (wf::input_event_signal<wlr_pointer_button_event> *ev)
    {
        if (ev->event->state != WL_POINTER_BUTTON_STATE_RELEASED)
        {
            return;
        }

        if (auto toplevel = wf::toplevel_cast(menu_view))
        {
            if (!toplevel->activated)
            {
                menu_view->close();
                menu_view   = nullptr;
                parent_view = nullptr;
            }
        }

        if (ev->event->button != BTN_RIGHT)
        {
            if (menu_view && (menu_view != wf::get_core().get_cursor_focus_view()))
            {
                on_view_unmapped.disconnect();
                menu_view->close();
                menu_view   = nullptr;
                parent_view = nullptr;
            }

            return;
        }

        auto focused_view = wf::get_core().get_cursor_focus_view();
        if (focused_view && (focused_view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT))
        {
            wf::view_show_window_menu_signal data;
            data.view = focused_view;
            on_show_menu.emit(&data);
            return;
        }

        parent_view = nullptr;
    };

    wf::signal::connection_t<wf::view_show_window_menu_signal> on_show_menu =
        [=] (wf::view_show_window_menu_signal *data)
    {
        if (workspace_switch_view && workspace_switch_progression.running())
        {
            workspace_switch_view->get_transformed_node()->rem_transformer(workspace_switch_transformer_name);
            workspace_switch_view = nullptr;
        }

        if (barrel_roll_view && barrel_roll_progression.running())
        {
            barrel_roll_view->get_transformed_node()->rem_transformer(barrel_roll_transformer_name);
            barrel_roll_view = nullptr;
        }

        if (menu_view)
        {
            menu_view->close();
            menu_view   = nullptr;
            parent_view = nullptr;
        }

        parent_view = data->view;
        if (parent_view)
        {
            parent_view->connect(&on_view_unmapped);
        }

        /* This assumes wf-menu is exectable and the containing directory is in $PATH */
        wf::get_core().run("wf-menu");
    };

    void send_menu_items()
    {
        uint32_t view_id = 0;
        if (parent_view)
        {
            view_id = parent_view->get_id();
        }

        // Send the menu items to the menu client
        wf_menu_manager_send_menu_items_start(menu_resource, view_id);
        for (size_t i = 0; i < menu_items.size(); i++)
        {
            auto item = menu_items[i];

            if (item.first == 0)
            {
                wf_menu_manager_send_menu_item(menu_resource, item.first, item.second.c_str());
                while (i < menu_items.size())
                {
                    auto subitem = menu_items[++i];
                    if ((subitem.first == 0) || subitem.second.empty())
                    {
                        break;
                    }

                    wf_menu_manager_send_submenu_item(menu_resource, subitem.first, subitem.second.c_str());
                }

                continue;
            }

            wf_menu_manager_send_menu_item(menu_resource, item.first, item.second.c_str());
        }

        wf_menu_manager_send_menu_corner_radius(menu_resource, corner_radius);
        wf_menu_manager_send_menu_items_done(menu_resource);
        menu_items.clear();
    }

    bool handle_desktop_action(std::string action)
    {
        if (action == DESKTOP_CHANGE_BG)
        {
            wf::get_core().run("killall -USR1 wf-background");
            parent_view = nullptr;
            return true;
        } else if (action == DESKTOP_LAUNCH_WCM)
        {
            wf::get_core().run("wcm");
            parent_view = nullptr;
            return true;
        } else if (action.substr(0, std::string("Workspace").size()) == "Workspace")
        {
            int x, y;
            int parsed = std::sscanf(action.c_str(), "Workspace [%d,%d]", &x, &y);
            if (parsed == 2)
            {
                if (auto output = wf::get_core().seat->get_active_output())
                {
                    output->wset()->request_workspace({x - 1, y - 1});
                    parent_view = nullptr;
                    return true;
                }
            }
        } else if (action == DESKTOP_CUBE_SPIN)
        {
            parent_view = nullptr;
            cube_spin_start();
            return true;
        } else if ((action == DESKTOP_DO_A_BARREL_ROLL) && parent_view &&
                   (parent_view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT))
        {
            barrel_roll_progression.start();

            barrel_roll_clockwise = false;
            if (wf::get_current_time() % 2)
            {
                barrel_roll_clockwise = true;
            }

            barrel_roll_view = parent_view;
            parent_view == nullptr;

            if (auto output = wf::get_core().seat->get_active_output())
            {
                auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(barrel_roll_view);
                barrel_roll_view->get_transformed_node()->add_transformer(
                    tr, wf::TRANSFORMER_2D, barrel_roll_transformer_name);
                output->render->add_effect(&barrel_roll_animation, wf::OUTPUT_EFFECT_PRE);
                return true;
            }
        }

        parent_view = nullptr;
        return false;
    }

    bool handle_wm_action(uint32_t view_id, std::string action)
    {
        wayfire_view view = nullptr;

        if (parent_view)
        {
            view = parent_view;
        } else
        {
            LOGD("No parent view for menu, trying view_id: ", view_id);
            for (auto v : wf::get_core().get_all_views())
            {
                if (view_id == v->get_id())
                {
                    parent_view = view = v;
                    break;
                }
            }
        }

        if (!view)
        {
            LOGD("No view found for menu.");
            return false;
        }

        if (action == WM_MINIMIZE)
        {
            wf::get_core().default_wm->minimize_request(wf::toplevel_cast(view), true);
            parent_view = nullptr;
            return true;
        } else if (action == WM_UNMAXIMIZE)
        {
            wf::get_core().default_wm->tile_request(wf::toplevel_cast(view), 0);
            parent_view = nullptr;
            return true;
        } else if (action == WM_MAXIMIZE)
        {
            wf::get_core().default_wm->tile_request(wf::toplevel_cast(view), wf::TILED_EDGES_ALL);
            parent_view = nullptr;
            return true;
        } else if (action == WM_CLOSE)
        {
            view->close();
            parent_view = nullptr;
            return true;
        } else if (action.substr(0, std::string("Workspace").size()) == "Workspace")
        {
            if (auto toplevel = wf::toplevel_cast(view))
            {
                int x, y;
                int parsed = std::sscanf(action.c_str(), "Workspace [%d,%d]", &x, &y);
                if (parsed == 2)
                {
                    if (auto output = view->get_output())
                    {
                        workspace_from_geometry = toplevel->get_geometry();
                        output->wset()->move_to_workspace(toplevel, {x - 1, y - 1});
                        workspace_to_geometry = toplevel->get_geometry();
                        workspace_switch_progression.start();

                        workspace_switch_view = view;
                        parent_view == nullptr;

                        auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(workspace_switch_view);
                        workspace_switch_view->get_transformed_node()->add_transformer(
                            tr, wf::TRANSFORMER_2D, workspace_switch_transformer_name);
                        output->render->add_effect(&workspace_switch_animation, wf::OUTPUT_EFFECT_PRE);
                        return true;
                    }
                }
            }
        } else if (action == WM_DO_A_BARREL_ROLL)
        {
            if (!parent_view)
            {
                return false;
            }

            barrel_roll_progression.start();

            barrel_roll_clockwise = false;
            if (wf::get_current_time() % 2)
            {
                barrel_roll_clockwise = true;
            }

            barrel_roll_view = parent_view;
            parent_view == nullptr;

            if (auto output = barrel_roll_view->get_output())
            {
                auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(barrel_roll_view);
                barrel_roll_view->get_transformed_node()->add_transformer(
                    tr, wf::TRANSFORMER_2D, barrel_roll_transformer_name);
                output->render->add_effect(&barrel_roll_animation, wf::OUTPUT_EFFECT_PRE);
                return true;
            }
        }

        parent_view = nullptr;
        return false;
    }

    void handle_action(uint32_t view_id, uint32_t action_id, std::string action)
    {
        if (handle_wm_action(view_id, action))
        {
            return;
        }

        handle_desktop_action(action);
    }

    void cube_spin_terminate()
    {
        cube_control_signal data;
        data.angle = 0.0;
        data.zoom  = CUBE_ZOOM_BASE;
        data.ease  = 0.0;
        data.last_frame  = true;
        data.carried_out = false;

        if (auto output = wf::get_core().seat->get_active_output())
        {
            output->emit(&data);
            if (cube_spin_hook_set)
            {
                output->render->rem_effect(&cube_spin_frame);
                cube_spin_hook_set = false;
            }
        }

        cube_state = CUBE_SPIN_DISABLED;
    }

    void cube_spin_start()
    {
        cube_control_signal data;
        data.angle = 0.0;
        data.zoom  = CUBE_ZOOM_BASE;
        data.ease  = 0.0;
        data.last_frame  = false;
        data.carried_out = false;

        if (auto output = wf::get_core().seat->get_active_output())
        {
            output->emit(&data);
            if (data.carried_out)
            {
                if (!cube_spin_hook_set)
                {
                    output->render->add_effect(
                        &cube_spin_frame, wf::OUTPUT_EFFECT_PRE);
                    cube_spin_hook_set = true;
                }
            }
        }

        cube_state = CUBE_SPIN_RUNNING;

        cube_spin_animation.rot.set(cube_spin_animation.rot, M_PI * 2.0);
        cube_spin_animation.zoom.set(CUBE_ZOOM_BASE, CUBE_ZOOM_MAX);
        cube_spin_animation.ease.set(0.0, 1.0);
        cube_spin_animation.start();
    }

    void cube_spin_stop()
    {
        cube_state = CUBE_SPIN_STOPPING;

        cube_spin_animation.rot.set(0.0, 0.0);
        cube_spin_animation.zoom.restart_with_end(CUBE_ZOOM_BASE);
        cube_spin_animation.ease.restart_with_end(0.0);
        cube_spin_animation.start();
    }

    wf::effect_hook_t cube_spin_frame = [=] ()
    {
        cube_control_signal data;

        if ((cube_state == CUBE_SPIN_STOPPING) && !cube_spin_animation.running())
        {
            cube_spin_terminate();
            return;
        }

        auto rotation = cube_spin_animation.rot;

        data.angle = rotation;
        data.zoom  = cube_spin_animation.zoom;
        data.ease  = cube_spin_animation.ease;
        data.last_frame  = false;
        data.carried_out = false;

        if (auto output = wf::get_core().seat->get_active_output())
        {
            output->emit(&data);
        }

        if (!data.carried_out)
        {
            cube_spin_terminate();

            return;
        }

        if (rotation >= M_PI * 2.0)
        {
            cube_spin_stop();
        }
    };

    wf::effect_hook_t workspace_switch_animation = [=] ()
    {
        if (!workspace_switch_view || !workspace_switch_progression.running())
        {
            if (workspace_switch_view)
            {
                if (auto output = workspace_switch_view->get_output())
                {
                    output->render->rem_effect(&workspace_switch_animation);
                }

                workspace_switch_view->get_transformed_node()->rem_transformer(
                    workspace_switch_transformer_name);
                workspace_switch_view = nullptr;
            }

            return;
        }

        auto transform = workspace_switch_view->get_transformed_node()
            ->get_transformer<wf::scene::view_2d_transformer_t>(workspace_switch_transformer_name);

        if (!transform)
        {
            workspace_switch_view->get_transformed_node()->rem_transformer(workspace_switch_transformer_name);
            if (auto output = workspace_switch_view->get_output())
            {
                output->render->rem_effect(&workspace_switch_animation);
            }

            return;
        }

        auto progress = workspace_switch_progression.progress();
        progress = 1.0 - std::pow(progress, 1.0 - progress);
        workspace_switch_view->get_transformed_node()->begin_transform_update();
        transform->translation_x = (workspace_from_geometry.x - workspace_to_geometry.x) * progress;
        transform->translation_y = (workspace_from_geometry.y - workspace_to_geometry.y) * progress;
        workspace_switch_view->get_transformed_node()->end_transform_update();

        if (auto output = workspace_switch_view->get_output())
        {
            output->render->schedule_redraw();
        }
    };

    wf::effect_hook_t barrel_roll_animation = [=] ()
    {
        if (!barrel_roll_view || !barrel_roll_progression.running())
        {
            if (barrel_roll_view)
            {
                if (auto output = barrel_roll_view->get_output())
                {
                    output->render->rem_effect(&barrel_roll_animation);
                }

                barrel_roll_view->get_transformed_node()->rem_transformer(barrel_roll_transformer_name);
                barrel_roll_view = nullptr;
            }

            return;
        }

        auto transform = barrel_roll_view->get_transformed_node()
            ->get_transformer<wf::scene::view_2d_transformer_t>(barrel_roll_transformer_name);

        if (!transform)
        {
            barrel_roll_view->get_transformed_node()->rem_transformer(barrel_roll_transformer_name);
            if (auto output = barrel_roll_view->get_output())
            {
                output->render->rem_effect(&barrel_roll_animation);
            }

            return;
        }

        auto progress = barrel_roll_progression.progress();
        std::pow(1.0 - progress, progress);
        if (barrel_roll_clockwise)
        {
            progress = 1.0 - progress;
        }

        barrel_roll_view->get_transformed_node()->begin_transform_update();
        transform->angle = progress * M_PI * 2.0;
        barrel_roll_view->get_transformed_node()->end_transform_update();

        if (auto output = barrel_roll_view->get_output())
        {
            output->render->schedule_redraw();
        }
    };

    void fini() override
    {
        on_view_mapped.disconnect();
        on_show_menu.disconnect();
        on_button_event.disconnect();
        wl_global_remove(menu_global);
        wl_global_destroy(menu_global);
    }
};
}
}

void handle_action_request(wl_client*, wl_resource *resource, uint32_t view_id, uint32_t action_id,
    const char *action)
{
    wf::menu::wf_menu *menu = (wf::menu::wf_menu*)wl_resource_get_user_data(resource);
    menu->handle_action(view_id, action_id, action);
}

static struct wf_menu_manager_interface wf_menu_impl =
{
    .action_request = handle_action_request,
};

static void handle_menu_destroy(wl_resource *listener)
{
    if (menu_view)
    {
        menu_resource = NULL;
        menu_view     = nullptr;
    }
}

static void bind_menu(wl_client *client, void *data, uint32_t, uint32_t id)
{
    wf::menu::wf_menu *menu = (wf::menu::wf_menu*)data;

    if (menu_resource)
    {
        if (menu_view)
        {
            menu->on_view_unmapped.disconnect();
            menu_view->close();
            menu_view   = nullptr;
            parent_view = nullptr;
        }
    }

    auto resource = wl_resource_create(client, &wf_menu_manager_interface, 1, id);

    wl_resource_set_implementation(resource, &wf_menu_impl, data, handle_menu_destroy);
    menu_resource = resource;

    parent_view ? menu->prepare_wm_menu() : menu->prepare_desktop_menu();
    menu->send_menu_items();
}

DECLARE_WAYFIRE_PLUGIN(wf::menu::wf_menu);

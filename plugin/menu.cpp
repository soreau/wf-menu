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
#include <wayfire/util/log.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>

#include "wf-menu-server-protocol.h"

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

std::vector<std::pair<uint32_t, std::string>> menu_items;

static wl_resource *menu_resource;
static wayfire_view menu_view, parent_view;
static void bind_menu(wl_client * client, void *data, uint32_t, uint32_t id);

class wf_menu : public wf::per_output_plugin_instance_t
{
    bool barrel_roll_clockwise;
    barrel_roll_animation_t barrel_roll_progression;
    workspace_switch_animation_t workspace_switch_progression;
    wf::geometry_t workspace_from_geometry, workspace_to_geometry;
    wl_global *menu_global;

  public:
    void init() override
    {
        menu_global = wl_global_create(wf::get_core().display,
            &wf_menu_manager_interface,
            1, this, bind_menu);

        wf::get_core().connect(&on_view_mapped);
        wf::get_core().connect(&on_show_menu);

        barrel_roll_progression = barrel_roll_animation_t(wf::create_option<int>(1500));
        workspace_switch_progression = workspace_switch_animation_t(wf::create_option<int>(1500));
    }

    void rebuild_menu()
    {
        int i = 1;
        auto workspace_grid_size = output->wset()->get_workspace_grid_size();

        menu_items.clear();

        menu_items.push_back({i++, "Minimize"});
        if (parent_view && wf::toplevel_cast(parent_view) &&
            wf::toplevel_cast(parent_view)->pending_tiled_edges())
        {
            menu_items.push_back({i++, "Unmaximize"});
        } else
        {
            menu_items.push_back({i++, "Maximize"});
        }

        menu_items.push_back({i++, "Close"});
        menu_items.push_back({0, "Send to Workspace"});
        for (int y = 1; y <= workspace_grid_size.height; y++)
        {
            for (int x = 1; x <= workspace_grid_size.width; x++)
            {
                menu_items.push_back({i++,
                    "Workspace [" + std::to_string(x) + "," + std::to_string(y) + "]"});
            }
        }

        menu_items.push_back({0, ""});
        menu_items.push_back({i++, "Do a Barrel Roll"});
    }

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        ev->view->set_role(wf::VIEW_ROLE_TOPLEVEL);
        auto toplevel = wf::toplevel_cast(ev->view);

        if (!toplevel)
        {
            return;
        }

        if (toplevel->get_app_id() == "wf-menu")
        {
            /* Skip taskbar */
            wf::view_unmapped_signal unmap_signal;
            unmap_signal.view = ev->view;
            wf::get_core().emit(&unmap_signal);
            /* Move top left of menu to mouse cursor position */
            auto pos = output->get_cursor_position();
            toplevel->move(pos.x, pos.y);
            menu_view = ev->view;
            menu_view->connect(&on_view_unmapped);
            menu_view->connect(&on_view_activated);
            wf::scene::readd_front(output->node_for_layer(wf::scene::layer::LOCK),
                menu_view->get_root_node());
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
            }

            parent_view = nullptr;

            return;
        }

        if (ev->view == menu_view)
        {
            menu_view = nullptr;
        }
    };

    wf::signal::connection_t<wf::view_activated_state_signal> on_view_activated =
        [=] (wf::view_activated_state_signal*)
    {
        if (auto toplevel = wf::toplevel_cast(menu_view))
        {
            if (!toplevel->activated)
            {
                toplevel->close();
                toplevel = nullptr;
            }
        }
    };

    wf::signal::connection_t<wf::view_show_window_menu_signal> on_show_menu =
        [=] (wf::view_show_window_menu_signal *data)
    {
        if (menu_view)
        {
            menu_view->close();
            menu_view = nullptr;
        }

        /* This assumes wf-menu is exectable and the containing directory is in $PATH */
        wf::get_core().run("wf-menu");
        parent_view = data->view;
        parent_view->connect(&on_view_unmapped);
        rebuild_menu();
    };

    void do_action(uint32_t view_id, uint32_t action_id, std::string action)
    {
        LOGI("action_request: ", action_id, ": ", action);

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
                    view = v;
                    break;
                }
            }
        }

        if (!view)
        {
            LOGD("No view found for menu.");
            return;
        }

        if (action == "Minimize")
        {
            wf::get_core().default_wm->minimize_request(wf::toplevel_cast(view), true);
        } else if (action == "Unmaximize")
        {
            wf::get_core().default_wm->tile_request(wf::toplevel_cast(view), 0);
        } else if (action == "Maximize")
        {
            wf::get_core().default_wm->tile_request(wf::toplevel_cast(view), wf::TILED_EDGES_ALL);
        } else if (action == "Close")
        {
            view->close();
        } else if (action.substr(0, std::string("Workspace").size()) == "Workspace")
        {
            if (auto toplevel = wf::toplevel_cast(view))
            {
                int x, y;
                int parsed = std::sscanf(action.c_str(), "Workspace [%d,%d]", &x, &y);
                if (parsed == 2)
                {
                    workspace_from_geometry = toplevel->get_geometry();
                    output->wset()->move_to_workspace(toplevel, {x - 1, y - 1});
                    workspace_to_geometry = toplevel->get_geometry();
                    workspace_switch_progression.start();

                    auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(view);
                    view->get_transformed_node()->add_transformer(
                        tr, wf::TRANSFORMER_2D, workspace_switch_transformer_name);
                    output->render->add_effect(&workspace_switch_animation_hook, wf::OUTPUT_EFFECT_PRE);
                }
            }
        } else if (action == "Do a Barrel Roll")
        {
            barrel_roll_progression.start();

            barrel_roll_clockwise = false;
            if (wf::get_current_time() % 2)
            {
                barrel_roll_clockwise = true;
            }

            auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(view);
            view->get_transformed_node()->add_transformer(
                tr, wf::TRANSFORMER_2D, barrel_roll_transformer_name);
            output->render->add_effect(&barrel_roll_animation_hook, wf::OUTPUT_EFFECT_PRE);
        }
    }

    wf::effect_hook_t workspace_switch_animation_hook = [=] ()
    {
        auto transform = parent_view->get_transformed_node()
            ->get_transformer<wf::scene::view_2d_transformer_t>(workspace_switch_transformer_name);
        auto progress = workspace_switch_progression.progress();
        progress = 1.0 - std::pow(progress, 1.0 - progress);
        parent_view->get_transformed_node()->begin_transform_update();
        transform->translation_x = (workspace_from_geometry.x - workspace_to_geometry.x) * progress;
        transform->translation_y = (workspace_from_geometry.y - workspace_to_geometry.y) * progress;
        parent_view->get_transformed_node()->end_transform_update();

        if (!workspace_switch_progression.running())
        {
            if (parent_view)
            {
                parent_view->get_transformed_node()->rem_transformer(workspace_switch_transformer_name);
            }

            output->render->rem_effect(&workspace_switch_animation_hook);
            return;
        }

        output->render->schedule_redraw();
    };

    wf::effect_hook_t barrel_roll_animation_hook = [=] ()
    {
        auto transform = parent_view->get_transformed_node()
            ->get_transformer<wf::scene::view_2d_transformer_t>(barrel_roll_transformer_name);
        auto progress = barrel_roll_progression.progress();
        std::pow(1.0 - progress, progress);
        if (barrel_roll_clockwise)
        {
            progress = 1.0 - progress;
        }

        parent_view->get_transformed_node()->begin_transform_update();
        transform->angle = progress * M_PI * 2.0;
        parent_view->get_transformed_node()->end_transform_update();

        if (!barrel_roll_progression.running())
        {
            if (parent_view)
            {
                parent_view->get_transformed_node()->rem_transformer(barrel_roll_transformer_name);
            }

            output->render->rem_effect(&barrel_roll_animation_hook);
            return;
        }

        output->render->schedule_redraw();
    };

    void fini() override
    {
        on_view_mapped.disconnect();
        on_show_menu.disconnect();
        wl_global_remove(menu_global);
        wl_global_destroy(menu_global);
    }
};

void handle_action_request(wl_client*, wl_resource *resource, uint32_t view_id, uint32_t action_id,
    const char *action)
{
    wf_menu *menu = (wf_menu*)wl_resource_get_user_data(resource);
    menu->do_action(view_id, action_id, action);
}

static struct wf_menu_manager_interface wf_menu_impl =
{
    .action_request = handle_action_request,
};

static void handle_menu_destroy(wl_resource *listener)
{
    menu_resource = NULL;
    menu_view     = nullptr;
}

static void bind_menu(wl_client *client, void *data, uint32_t, uint32_t id)
{
    if (menu_resource)
    {
        return;
    }

    LOGI("Binding wf-menu");
    auto resource = wl_resource_create(client, &wf_menu_manager_interface, 1, id);

    wl_resource_set_implementation(resource, &wf_menu_impl, data, handle_menu_destroy);
    menu_resource = resource;

    auto active_view = wf::get_core().seat->get_active_view();
    uint32_t view_id = 0;
    if (active_view)
    {
        view_id     = active_view->get_id();
        parent_view = active_view;
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

    wf_menu_manager_send_menu_items_done(menu_resource);
}

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf_menu>);

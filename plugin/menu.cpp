/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Scott Moreau
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
#include <wayfire/plugin.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>

#include <linux/input-event-codes.h>


class wf_menu : public wf::per_output_plugin_instance_t
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;
    wayfire_view parent_view = nullptr;
    wayfire_view parent_menu = nullptr;
    wayfire_view child_menu = nullptr;
  public:
    void init() override
    {
        wf::get_core().connect(&on_view_mapped);
        wf::get_core().connect(&on_show_menu);
        ipc_repo->register_method("wf/menu/actions", ipc_do_action);
    }

    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_button_event>> on_button_event =
        [=] (wf::input_event_signal<wlr_pointer_button_event> *ev)
    {
        if (ev->event->state != WL_POINTER_BUTTON_STATE_PRESSED)
        {
            return;
        }
        if (parent_menu)
        {
            auto offset = wf::origin(output->get_layout_geometry());
            auto at = wf::get_core().get_cursor_position();
            at.x -= offset.x;
            at.y -= offset.y;
            auto view = wf::get_core().get_view_at(at);
            if (view != parent_menu && view != child_menu)
            {
                parent_menu->close();
                parent_menu = nullptr;
                on_button_event.disconnect();
            }
        }
    };

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        ev->view->set_role(wf::VIEW_ROLE_TOPLEVEL);
        auto toplevel = wf::toplevel_cast(ev->view);

        if (!toplevel)
        {
            return;
        }
        auto pos = wf::get_core().get_cursor_position();
        if (toplevel->get_app_id() == "wf-menu")
        {
            /* Skip taskbar */
            wf::view_unmapped_signal unmap_signal;
            unmap_signal.view = ev->view;
            wf::get_core().emit(&unmap_signal);
            if (!parent_menu)
            {
                /* Move top left of menu to mouse cursor position */
                toplevel->move(pos.x, pos.y);
                parent_menu = ev->view;
                wf::get_core().connect(&on_button_event);
                wf::get_core().connect(&on_view_unmap);
            } else {
                auto parent_toplevel = wf::toplevel_cast(parent_menu);
                if (parent_toplevel)
                {
                    auto g = parent_toplevel->get_geometry();
                    toplevel->move(g.x + g.width, pos.y - 10);
                    child_menu = ev->view;
                }
            }
            wf::scene::readd_front(output->node_for_layer(
                wf::scene::layer::LOCK),
                ev->view->get_root_node());
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmap = [=] (wf::view_unmapped_signal *ev)
    {
        if (ev->view.get() == parent_menu.get())
        {LOGI("unmapped");
            on_view_unmap.disconnect();
            parent_menu = nullptr;
        }
    };

    wf::signal::connection_t<wf::view_show_window_menu_signal> on_show_menu = [=] (wf::view_show_window_menu_signal *data)
    {
        /* This assumes wf-menu.py is exectable and the containing directory is in $PATH */
        wf::get_core().run("wf-menu.py");
        parent_view = data->view;
    };

    wf::ipc::method_callback ipc_do_action = [=] (wf::json_t data) -> wf::json_t
    {
        if (!parent_view)
        {
            parent_menu->close();
            parent_menu = nullptr;
            return wf::ipc::json_error("View not found.");
        }
        auto toplevel = wf::toplevel_cast(parent_view);
        if (!toplevel)
        {
            return wf::ipc::json_error("Toplevel not found.");
        }
        auto action = wf::ipc::json_get_string(data, "action");

        if (action == "close")
        {
            parent_view->close();
            parent_view = nullptr;
        } else if (action == "minimize")
        {
            wf::get_core().default_wm->minimize_request(toplevel, true);
        } else if (action == "restore")
        {
            if (toplevel->minimized)
            {
                wf::get_core().default_wm->minimize_request(toplevel, false);
            } else {
                if (toplevel->pending_tiled_edges() != 0)
                {
                    wf::get_core().default_wm->tile_request(toplevel, 0);
                }
            }
        } else if (action == "maximize")
        {
            if (toplevel->pending_tiled_edges() != wf::TILED_EDGES_ALL)
            {
                wf::get_core().default_wm->tile_request(toplevel, wf::TILED_EDGES_ALL);
            }
        } else if (action == "to_workspace")
        {
            auto ws = wf::ipc::json_get_int64(data, "workspace") - 1;
            auto og    = output->get_relative_geometry();
            auto wsize = output->wset()->get_workspace_grid_size();
            auto ows   = output->wset()->get_view_main_workspace(toplevel);
            auto nws   = wf::point_t{int(ws % wsize.width), int(ws / wsize.width)};
            auto vg = toplevel->get_geometry();
            LOGI("ows: ", ows);
            LOGI("nws: ", nws);
            toplevel->move(vg.x + (nws.x - ows.x) * og.width, vg.y + (nws.y - ows.y) * og.height);
        } else if (action == "next_output")
        {
            LOGI("next_output");
        } else if (action == "prev_output")
        {
            LOGI("prev_output");
        } else if (action == "layer")
        {
            auto desktop_layer = wf::ipc::json_get_string(data, "layer");
            if (desktop_layer == "Background")
            {
                wf::scene::readd_front(output->node_for_layer(
                    wf::scene::layer::BACKGROUND),
                    parent_view->get_root_node());
            } else if (desktop_layer == "Bottom")
            {
                wf::scene::readd_front(output->node_for_layer(
                    wf::scene::layer::BOTTOM),
                    parent_view->get_root_node());
            } else if (desktop_layer == "Workspace")
            {
                wf::scene::readd_front(output->wset()->get_node(),
                    parent_view->get_root_node());
            } else if (desktop_layer == "Top")
            {
                wf::scene::readd_front(output->node_for_layer(
                    wf::scene::layer::TOP),
                    parent_view->get_root_node());
            } else if (desktop_layer == "Unmanaged")
            {
                wf::scene::readd_front(output->node_for_layer(
                    wf::scene::layer::UNMANAGED),
                    parent_view->get_root_node());
            } else if (desktop_layer == "Overlay")
            {
                wf::scene::readd_front(output->node_for_layer(
                    wf::scene::layer::OVERLAY),
                    parent_view->get_root_node());
            } else if (desktop_layer == "Lock")
            {
                wf::scene::readd_front(output->node_for_layer(
                    wf::scene::layer::LOCK),
                    parent_view->get_root_node());
            } else
            {
                return wf::ipc::json_error("Layer not found: " + desktop_layer);
            }
        } else if (action == "shade_toggle")
        {
            LOGI("shade_toggle");
            ipc_repo->call_method("pixdecor/shade_toggle", "");
        }

        if (parent_menu)
        {
            parent_menu->close();
        }

        return wf::ipc::json_ok();
    };

    void fini() override
    {
        ipc_repo->unregister_method("wf/menu/actions");
        on_button_event.disconnect();
        on_view_mapped.disconnect();
        on_view_unmap.disconnect();
        on_show_menu.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf_menu>);

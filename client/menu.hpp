#pragma once

#include <gtkmm/window.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/revealer.h>
#include <gdk/wayland/gdkwayland.h>
#include <giomm/simpleactiongroup.h>
#include <vector>
#include <string>

struct MenuItemConfig
{
    std::pair<uint32_t, std::string> label;
    std::vector<std::pair<uint32_t, std::string>> submenu_items; // If empty, it's a regular action item
};


class DynamicMenuWindow : public Gtk::Window
{
  public:
    DynamicMenuWindow();
    virtual ~DynamicMenuWindow();

    wl_display *display;

    // Call this before showing the window to populate items dynamically
    void set_menu_structure(const std::vector<MenuItemConfig>& config);
    void toggle_submenu(Gtk::Revealer *revealer);
    uint32_t corner_radius;

  protected:
    // Signal handlers
    void on_menu_item_clicked(const std::string& item_name, uint32_t action_id);
    bool on_window_focusable_change();

  private:
    Gtk::Box m_main_box;
    Glib::RefPtr<Gio::SimpleActionGroup> m_action_group;
};

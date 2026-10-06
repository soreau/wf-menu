#include "menu.hpp"
#include <iostream>
#include <wayland-client.h>
#include <gdk/wayland/gdkwayland.h>

#include "wf-menu-client-protocol.h"

static wf_menu_manager *menu_manager;
static std::vector<MenuItemConfig> menu_structure;
static uint32_t active_view_id;

static void handle_menu_items_start(void *data, wf_menu_manager *wf_menu_manager, uint32_t view_id)
{
    printf("%s\n", __func__);
    active_view_id = view_id;
}

static void handle_menu_item(void *data, wf_menu_manager *wf_menu_manager, uint32_t action_id,
    const char *item)
{
    printf("%s\n", __func__);
    MenuItemConfig menu_item;
    menu_item.label = {action_id, item};
    menu_structure.push_back(menu_item);
}

static void handle_submenu_item(void *data, wf_menu_manager *wf_menu_manager, uint32_t action_id,
    const char *item)
{
    printf("%s\n", __func__);
    MenuItemConfig *menu_item = &menu_structure.back();
    menu_item->submenu_items.push_back({action_id, item});
}

static void handle_menu_items_done(void *data, wf_menu_manager *wf_menu_manager)
{
    printf("%s\n", __func__);
    DynamicMenuWindow *wf_menu = (DynamicMenuWindow*)data;
    wf_menu->set_menu_structure(menu_structure);
}

static wf_menu_manager_listener wf_menu_impl = {
    .menu_items_start = handle_menu_items_start,
    .menu_item    = handle_menu_item,
    .submenu_item = handle_submenu_item,
    .menu_items_done = handle_menu_items_done,
};

static void registry_add_object(void *data, wl_registry *registry, uint32_t name,
    const char *interface, uint32_t version)
{
    DynamicMenuWindow *wf_menu = (DynamicMenuWindow*)data;

    if (strcmp(interface, wf_menu_manager_interface.name) == 0)
    {
        menu_manager = (wf_menu_manager*)
            wl_registry_bind(registry, name,
            &wf_menu_manager_interface,
            version);
        wf_menu_manager_add_listener(menu_manager,
            &wf_menu_impl, wf_menu);
    }
}

static void registry_remove_object(void *data, struct wl_registry *registry, uint32_t name)
{}

static struct wl_registry_listener registry_listener =
{
    &registry_add_object,
    &registry_remove_object
};

DynamicMenuWindow::DynamicMenuWindow() :
    m_main_box(Gtk::Orientation::VERTICAL, 2)
{
    // Configure window to look and behave like a standalone menu popup
    set_title("Dynamic Menu");
    set_decorated(false); // Removes title bar and borders
    set_resizable(false); // Prevent resizing
    set_hide_on_close(true); // Hide instead of destroying if closed

    // Add main layout container
    set_child(m_main_box);
    m_main_box.set_margin(5);

    // Create an action group to handle dynamic menu item triggers
    m_action_group = Gio::SimpleActionGroup::create();
    insert_action_group("wf-menu", m_action_group);

    auto gdk_display = gdk_display_get_default();
    auto display     = gdk_wayland_display_get_wl_display(gdk_display);

    auto registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, this);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);

    set_menu_structure(menu_structure);
}

DynamicMenuWindow::~DynamicMenuWindow()
{}

void DynamicMenuWindow::set_menu_structure(const std::vector<MenuItemConfig>& config)
{
    // Clear old widgets
    auto *child = m_main_box.get_first_child();
    while (child)
    {
        auto *next = child->get_next_sibling();
        m_main_box.remove(*child);
        child = next;
    }

    for (const auto& item : config)
    {
        // Case A: This item has a submenu
        if (!item.submenu_items.empty())
        {
            // Container for the item button + its hidden submenu box
            auto *item_container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

            // The trigger button (displays an arrow icon or text indicator)
            auto *trigger_button = Gtk::make_managed<Gtk::Button>(item.label.second + "  ▸");
            trigger_button->set_has_frame(false);
            trigger_button->set_halign(Gtk::Align::START);

            // A Revealer creates a smooth expand/collapse transition for the nested items
            auto *submenu_revealer = Gtk::make_managed<Gtk::Revealer>();
            submenu_revealer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);

            // The box holding actual submenu items (indented slightly for visual hierarchy)
            auto *submenu_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
            submenu_box->set_margin_start(15);

            for (const auto& sub_label : item.submenu_items)
            {
                std::string sub_action_id = sub_label.second;
                std::replace(sub_action_id.begin(), sub_action_id.end(), ' ', '_');
                std::transform(sub_action_id.begin(), sub_action_id.end(), sub_action_id.begin(), ::tolower);

                if (!m_action_group->has_action(sub_action_id))
                {
                    m_action_group->add_action(sub_action_id, sigc::bind(
                        sigc::mem_fun(*this, &DynamicMenuWindow::on_menu_item_clicked), sub_label.second,
                        sub_label.first));
                }

                auto *sub_button = Gtk::make_managed<Gtk::Button>(sub_label.second);
                sub_button->set_has_frame(false);
                sub_button->set_halign(Gtk::Align::START);
                sub_button->set_action_name("wf-menu." + sub_action_id);

                submenu_box->append(*sub_button);
            }

            submenu_revealer->set_child(*submenu_box);

            // Connect clicking the trigger button to sliding open the submenu layout
            trigger_button->signal_clicked().connect(sigc::bind(
                sigc::mem_fun(*this, &DynamicMenuWindow::toggle_submenu), submenu_revealer));

            item_container->append(*trigger_button);
            item_container->append(*submenu_revealer);
            m_main_box.append(*item_container);
        }
        // Case B: Regular Action Item
        else
        {
            std::string action_id = item.label.second;
            std::replace(action_id.begin(), action_id.end(), ' ', '_');
            std::transform(action_id.begin(), action_id.end(), action_id.begin(), ::tolower);

            if (!m_action_group->has_action(action_id))
            {
                m_action_group->add_action(action_id, sigc::bind(
                    sigc::mem_fun(*this, &DynamicMenuWindow::on_menu_item_clicked), item.label.second,
                    item.label.first));
            }

            auto *button = Gtk::make_managed<Gtk::Button>(item.label.second);
            button->set_has_frame(false);
            button->set_halign(Gtk::Align::START);
            button->set_action_name("wf-menu." + action_id);

            m_main_box.append(*button);
        }
    }
}

void DynamicMenuWindow::toggle_submenu(Gtk::Revealer *revealer)
{
    // Flip the visibility state of the nested submenu container
    revealer->set_reveal_child(!revealer->get_reveal_child());
}

void DynamicMenuWindow::on_menu_item_clicked(const std::string& item_name, uint32_t action_id)
{
    std::cout << "Menu item activated: " << action_id << ": " << item_name << std::endl;
    wf_menu_manager_action_request(menu_manager, active_view_id, action_id, item_name.c_str());
    close(); // Close menu on leaf item selection
}

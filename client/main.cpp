#include <gtkmm/application.h>
#include "menu.hpp"

int main(int argc, char *argv[])
{
    auto app = Gtk::Application::create("wf-menu");
    app->make_window_and_run<DynamicMenuWindow>(argc, argv);
}

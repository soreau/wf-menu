#pragma once

/* Taken from wayfire wm-actions plugin */

class always_on_top_root_node_t : public wf::scene::output_node_t
{
  public:
    using output_node_t::output_node_t;

    std::string stringify() const override
    {
        return "always-on-top for output " + get_output()->to_string() + " " + stringify_flags();
    }
};

class always_on_bottom_root_node_t : public wf::scene::output_node_t
{
  public:
    using output_node_t::output_node_t;

    std::string stringify() const override
    {
        return "always-on-bottom for output " + get_output()->to_string() + " " + stringify_flags();
    }
};

void emit_above_changed(wf::output_t *output, wayfire_view view)
{
    if (!output)
    {
        return;
    }

    wf::wm_actions_above_changed_signal data;
    data.view = view;
    output->emit(&data);
}

void emit_below_changed(wf::output_t *output, wayfire_view view)
{
    if (!output)
    {
        return;
    }

    wf::wm_actions_below_changed_signal data;
    data.view = view;
    output->emit(&data);
}

bool unset_above_state_data(wf::output_t *output, wayfire_view view)
{
    if (!view->has_data("wm-actions-above"))
    {
        return false;
    }

    view->erase_data("wm-actions-above");
    emit_above_changed(output, view);
    return true;
}

bool unset_below_state_data(wf::output_t *output, wayfire_view view)
{
    if (!view->has_data("wm-actions-below"))
    {
        return false;
    }

    view->erase_data("wm-actions-below");
    emit_below_changed(output, view);
    return true;
}

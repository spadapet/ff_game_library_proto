#pragma once

#include "source/ui/dialog_content_base.h"

namespace editor
{
    class save_project_dialog : public editor::dialog_content_base
    {
    public:
        save_project_dialog();

        virtual bool on_window_close(ff::window* window) override;

    protected:
        virtual bool has_close_command(int result);

    private:
        NS_DECLARE_REFLECTION(editor::save_project_dialog, editor::dialog_content_base);
    };
}

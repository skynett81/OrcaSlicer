#pragma once

#include <wx/dialog.h>
#include <wx/listbox.h>

class wxButton;
class wxStaticText;

namespace Slic3r { namespace GUI {

// Forge Library — entry point for 3DPrintForge's 51 parametric model
// generators (Gridfinity, gears, lithophanes, NFC tags…) from inside
// the slicer GUI. Phase 1: catalog of generator names + an "Open in
// 3DPrintForge" affordance that launches the dashboard's Model Forge
// page. Phase 2 will add an embedded HTTP client that posts to
// /api/model-forge/{id}/generate-3mf and drops the result onto the
// build plate without leaving the slicer.
class ForgeLibraryDialog : public wxDialog {
public:
    explicit ForgeLibraryDialog(wxWindow* parent);

private:
    wxListBox*    m_list = nullptr;
    wxButton*     m_btn_open = nullptr;
    wxButton*     m_btn_generate = nullptr;
    wxStaticText* m_hint = nullptr;

    void on_open_in_browser(wxCommandEvent& evt);
    void on_generate_default(wxCommandEvent& evt);
};

}} // namespace Slic3r::GUI

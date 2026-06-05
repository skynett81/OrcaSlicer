#pragma once

#include <wx/dialog.h>
#include <wx/checklst.h>

class wxButton;
class wxStaticText;
class wxTextCtrl;
class wxChoice;

namespace Slic3r { namespace GUI {

// Native printer onboarding — replaces OrcaSlicer's webkit2gtk-driven
// ConfigWizard whose webview renders blank on NVIDIA + Wayland.
// Lists every vendor's printers as a checklist; user ticks the ones
// they actually have, hits OK, and we record the selection in
// AppConfig and refresh PresetBundle so the dropdowns surface them.
class ForgeOnboardingDialog : public wxDialog {
public:
    explicit ForgeOnboardingDialog(wxWindow* parent);

private:
    wxChoice*     m_vendor_filter = nullptr;
    wxTextCtrl*   m_search = nullptr;
    wxCheckListBox* m_list = nullptr;
    wxStaticText* m_count_label = nullptr;
    wxButton*     m_btn_select_all = nullptr;
    wxButton*     m_btn_clear = nullptr;
    wxButton*     m_btn_ok = nullptr;

    // Each entry is "<vendor> <model> <nozzle> nozzle"
    std::vector<std::tuple<std::string,std::string,std::string>> m_all;
    std::vector<int> m_filtered_indices;  // m_list row -> m_all index

    void load_vendor_catalog();
    void apply_filter();
    void on_filter_changed(wxCommandEvent& evt);
    void on_select_all(wxCommandEvent& evt);
    void on_clear(wxCommandEvent& evt);
    void on_ok(wxCommandEvent& evt);
    void update_count_label();
};

}} // namespace Slic3r::GUI

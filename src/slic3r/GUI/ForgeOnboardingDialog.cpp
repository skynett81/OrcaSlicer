#include "ForgeOnboardingDialog.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"

#include "libslic3r_version.h"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/msgdlg.h>

#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <set>
#include <algorithm>

namespace Slic3r { namespace GUI {

ForgeOnboardingDialog::ForgeOnboardingDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY,
               wxString::Format(_L("%s — Choose your printers"), SLIC3R_APP_NAME),
               wxDefaultPosition, wxSize(640, 720),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    // Inherit the app's themed background instead of forcing white — on the dark
    // theme a white background turned the light labels/controls white-on-white
    // (the "hard to see white boxes"). UpdateDlgDarkUI() below themes it properly.

    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, _L("Pick the printers you own"));
    auto title_font = title->GetFont();
    title_font.SetPointSize(title_font.GetPointSize() + 3);
    title_font.MakeBold();
    title->SetFont(title_font);
    root->Add(title, 0, wxALL, 14);

    auto* hint = new wxStaticText(this, wxID_ANY,
        _L("Tick every printer + nozzle combination you actually have. "
           "The slicer's printer dropdown will only show what's checked here. "
           "You can re-open this from the Forge menu."));
    hint->Wrap(560);
    root->Add(hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 14);

    // Filter row: vendor dropdown + search text
    auto* filter_row = new wxBoxSizer(wxHORIZONTAL);
    filter_row->Add(new wxStaticText(this, wxID_ANY, _L("Vendor:")),
                    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_vendor_filter = new wxChoice(this, wxID_ANY);
    m_vendor_filter->Append(_L("All vendors"));
    filter_row->Add(m_vendor_filter, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);
    filter_row->Add(new wxStaticText(this, wxID_ANY, _L("Search:")),
                    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_search = new wxTextCtrl(this, wxID_ANY);
    filter_row->Add(m_search, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(filter_row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 14);

    m_list = new wxCheckListBox(this, wxID_ANY);
    root->Add(m_list, 1, wxLEFT | wxRIGHT | wxEXPAND, 14);

    auto* counter_row = new wxBoxSizer(wxHORIZONTAL);
    m_count_label = new wxStaticText(this, wxID_ANY, "");
    counter_row->Add(m_count_label, 1, wxALIGN_CENTER_VERTICAL);
    m_btn_select_all = new wxButton(this, wxID_ANY, _L("Select all (filtered)"));
    m_btn_clear = new wxButton(this, wxID_ANY, _L("Clear"));
    counter_row->Add(m_btn_select_all, 0, wxRIGHT, 6);
    counter_row->Add(m_btn_clear, 0);
    root->Add(counter_row, 0, wxALL | wxEXPAND, 14);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    m_btn_ok = new wxButton(this, wxID_OK, _L("Install selected & continue"));
    auto* cancel = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    buttons->AddStretchSpacer(1);
    buttons->Add(cancel, 0, wxRIGHT, 8);
    buttons->Add(m_btn_ok, 0);
    root->Add(buttons, 0, wxALL | wxEXPAND, 14);

    SetSizer(root);

    load_vendor_catalog();
    apply_filter();

    m_vendor_filter->Bind(wxEVT_CHOICE, &ForgeOnboardingDialog::on_filter_changed, this);
    m_search->Bind(wxEVT_TEXT, &ForgeOnboardingDialog::on_filter_changed, this);
    m_list->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent&) { update_count_label(); });
    m_btn_select_all->Bind(wxEVT_BUTTON, &ForgeOnboardingDialog::on_select_all, this);
    m_btn_clear->Bind(wxEVT_BUTTON, &ForgeOnboardingDialog::on_clear, this);
    m_btn_ok->Bind(wxEVT_BUTTON, &ForgeOnboardingDialog::on_ok, this);

    wxGetApp().UpdateDlgDarkUI(this); // theme the dialog (dark/light) so text contrasts
    CentreOnParent();
}

void ForgeOnboardingDialog::load_vendor_catalog()
{
    namespace fs = boost::filesystem;
    fs::path system_dir = fs::path(Slic3r::data_dir()) / "system";
    if (!fs::exists(system_dir)) return;

    std::set<std::string> vendors_sorted;

    for (fs::directory_iterator it(system_dir), end; it != end; ++it) {
        if (!fs::is_regular_file(it->path())) continue;
        if (it->path().extension() != ".json") continue;
        const std::string vendor_name = it->path().stem().string();
        if (vendor_name == "Custom" || vendor_name == "OrcaFilamentLibrary") continue;

        try {
            std::ifstream ifs(it->path().string());
            nlohmann::json vendor_json;
            ifs >> vendor_json;
            if (!vendor_json.contains("machine_model_list")) continue;
            for (const auto& m : vendor_json["machine_model_list"]) {
                if (!m.contains("name") || !m.contains("sub_path")) continue;
                const std::string model_name = m["name"].get<std::string>();
                fs::path machine_path = system_dir / vendor_name / m["sub_path"].get<std::string>();
                if (!fs::exists(machine_path)) continue;
                std::ifstream mifs(machine_path.string());
                nlohmann::json machine_json;
                mifs >> machine_json;
                if (!machine_json.contains("nozzle_diameter")) continue;
                std::string nozzles = machine_json["nozzle_diameter"].get<std::string>();
                std::string nozzle;
                for (size_t i = 0; i <= nozzles.size(); ++i) {
                    if (i == nozzles.size() || nozzles[i] == ';') {
                        if (!nozzle.empty()) {
                            m_all.emplace_back(vendor_name, model_name, nozzle);
                            vendors_sorted.insert(vendor_name);
                        }
                        nozzle.clear();
                    } else {
                        nozzle.push_back(nozzles[i]);
                    }
                }
            }
        } catch (...) { /* skip malformed vendor */ }
    }

    std::sort(m_all.begin(), m_all.end());
    for (const auto& v : vendors_sorted) m_vendor_filter->Append(v);

    // Pre-tick what's already installed in AppConfig so the dialog
    // shows current state instead of being empty every time.
    if (wxGetApp().app_config) {
        // Pre-tick is applied after apply_filter populates the list.
    }
}

void ForgeOnboardingDialog::apply_filter()
{
    wxString vendor_sel = m_vendor_filter->GetStringSelection();
    wxString search_text = m_search->GetValue().Lower();
    bool all_vendors = (vendor_sel == _L("All vendors") || vendor_sel.IsEmpty());

    m_filtered_indices.clear();
    wxArrayString items;
    for (size_t i = 0; i < m_all.size(); ++i) {
        const auto& [v, m, n] = m_all[i];
        if (!all_vendors && wxString::FromUTF8(v) != vendor_sel) continue;
        wxString label = wxString::Format("%s  —  %s  (%s nozzle)",
                                          wxString::FromUTF8(v),
                                          wxString::FromUTF8(m),
                                          wxString::FromUTF8(n));
        if (!search_text.IsEmpty() && !label.Lower().Contains(search_text)) continue;
        m_filtered_indices.push_back((int)i);
        items.Add(label);
    }
    m_list->Set(items);

    if (wxGetApp().app_config) {
        for (size_t row = 0; row < m_filtered_indices.size(); ++row) {
            const auto& [v, mm, n] = m_all[m_filtered_indices[row]];
            std::string variant = n + " nozzle";
            if (wxGetApp().app_config->get_variant(v, mm, variant)) {
                m_list->Check((unsigned)row, true);
            }
        }
    }

    update_count_label();
}

void ForgeOnboardingDialog::on_filter_changed(wxCommandEvent& /*evt*/)
{
    apply_filter();
}

void ForgeOnboardingDialog::on_select_all(wxCommandEvent& /*evt*/)
{
    for (unsigned i = 0; i < m_list->GetCount(); ++i) m_list->Check(i, true);
    update_count_label();
}

void ForgeOnboardingDialog::on_clear(wxCommandEvent& /*evt*/)
{
    for (unsigned i = 0; i < m_list->GetCount(); ++i) m_list->Check(i, false);
    update_count_label();
}

void ForgeOnboardingDialog::update_count_label()
{
    int total_filtered = (int)m_list->GetCount();
    int checked = 0;
    for (unsigned i = 0; i < m_list->GetCount(); ++i)
        if (m_list->IsChecked(i)) ++checked;
    m_count_label->SetLabel(wxString::Format(_L("%d shown, %d ticked across %zu total in catalogue"),
                                              total_filtered, checked, m_all.size()));
}

void ForgeOnboardingDialog::on_ok(wxCommandEvent& /*evt*/)
{
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg) { EndModal(wxID_CANCEL); return; }

    // Apply the visible checklist on top of the existing config. Rows
    // outside the current filter are left alone, so the user can
    // narrow by vendor, tick a few, switch vendor and tick more
    // without losing earlier choices.
    int touched = 0;
    for (size_t row = 0; row < m_filtered_indices.size(); ++row) {
        const auto& [v, m, n] = m_all[m_filtered_indices[row]];
        std::string variant = n + " nozzle";
        cfg->set_variant(v, m, variant, m_list->IsChecked((unsigned)row));
        ++touched;
    }
    cfg->save();

    // Force the slicer to re-read presets so the dropdowns refresh.
    try {
        wxGetApp().load_current_presets();
    } catch (...) { /* best-effort, user can also just restart */ }

    EndModal(wxID_OK);
}

}} // namespace Slic3r::GUI

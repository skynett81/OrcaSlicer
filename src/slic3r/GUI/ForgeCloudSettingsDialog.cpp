#include "ForgeCloudSettingsDialog.hpp"
#include "ForgeCloud.hpp"
#include "I18N.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <wx/arrstr.h>

namespace Slic3r { namespace GUI {

ForgeCloudSettingsDialog::ForgeCloudSettingsDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Cloud & Remote Settings"),
               wxDefaultPosition, wxSize(560, 460),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    build_ui();
    populate_providers();
    CenterOnParent();
}

void ForgeCloudSettingsDialog::build_ui()
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* heading = new wxStaticText(this, wxID_ANY, _L("3DPrintForge dashboard"));
    auto hf = heading->GetFont(); hf.MakeBold(); heading->SetFont(hf);
    root->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, 16);

    auto* hint = new wxStaticText(this, wxID_ANY,
        _L("Shared by the Fleet panel, Forge Library and \"Send to 3DPrintForge\"."));
    root->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, 16);

    auto* url_row = new wxBoxSizer(wxHORIZONTAL);
    url_row->Add(new wxStaticText(this, wxID_ANY, _L("Dashboard URL:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    m_url = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(forge_dashboard_url()));
    url_row->Add(m_url, 1, wxALIGN_CENTER_VERTICAL);
    auto* btn_test = new wxButton(this, wxID_ANY, _L("Test"));
    url_row->Add(btn_test, 0, wxLEFT, 8);
    root->Add(url_row, 0, wxEXPAND | wxALL, 16);

    auto* prov_heading = new wxStaticText(this, wxID_ANY, _L("Cloud providers"));
    auto pf = prov_heading->GetFont(); pf.MakeBold(); prov_heading->SetFont(pf);
    root->Add(prov_heading, 0, wxLEFT | wxRIGHT, 16);

    m_providers = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL);
    m_providers->InsertColumn(0, _L("Provider"), wxLIST_FORMAT_LEFT, 260);
    m_providers->InsertColumn(1, _L("Status"),   wxLIST_FORMAT_LEFT, 220);
    root->Add(m_providers, 1, wxEXPAND | wxALL, 16);

    auto* note = new wxStaticText(this, wxID_ANY,
        _L("OctoEverywhere, Obico, SimplyPrint, Prusa Connect and Bambu Cloud arrive in a later phase."));
    root->Add(note, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);

    auto* btns = new wxBoxSizer(wxHORIZONTAL);
    btns->AddStretchSpacer();
    auto* btn_save   = new wxButton(this, wxID_OK,     _L("Save"));
    auto* btn_cancel = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    btns->Add(btn_save, 0, wxRIGHT, 8);
    btns->Add(btn_cancel, 0);
    root->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

    SetSizer(root);

    btn_test->Bind(wxEVT_BUTTON, &ForgeCloudSettingsDialog::on_test, this);
    btn_save->Bind(wxEVT_BUTTON, &ForgeCloudSettingsDialog::on_save, this);
}

void ForgeCloudSettingsDialog::populate_providers()
{
    m_providers->DeleteAllItems();
    long row = 0;
    for (CloudProvider* p : cloud_providers()) {
        long i = m_providers->InsertItem(row++, wxString::FromUTF8(p->display_name()));
        m_providers->SetItem(i, 1, p->is_configured() ? _L("Configured") : _L("Not configured"));
    }
}

void ForgeCloudSettingsDialog::on_test(wxCommandEvent& /*evt*/)
{
    const std::string url = m_url->GetValue().ToStdString();
    // Reachability check: hit the base URL, accept self-signed certs.
    wxString cmd = wxString::Format(
        "curl -ks -o /dev/null -w \"%%{http_code}\" \"%s\" --max-time 8",
        wxString::FromUTF8(url.c_str()));
    wxArrayString out, err;
    wxBusyCursor wait;
    long ec = wxExecute(cmd, out, err, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE);
    if (ec == 0 && !out.IsEmpty() && out[0] != "000")
        wxMessageBox(wxString::Format(_L("Reachable — HTTP %s"), out[0]),
                     _L("Cloud & Remote Settings"), wxOK | wxICON_INFORMATION, this);
    else
        wxMessageBox(_L("Could not reach the dashboard. Is 3DPrintForge running?"),
                     _L("Cloud & Remote Settings"), wxOK | wxICON_ERROR, this);
}

void ForgeCloudSettingsDialog::on_save(wxCommandEvent& evt)
{
    std::string url = m_url->GetValue().ToStdString();
    // Trim trailing slash for consistent path joins.
    while (!url.empty() && url.back() == '/') url.pop_back();
    set_forge_dashboard_url(url);
    evt.Skip(); // let wxID_OK close the dialog
}

}} // namespace Slic3r::GUI

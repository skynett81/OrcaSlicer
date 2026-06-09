#include "ForgeCloudSettingsDialog.hpp"
#include "ForgeCloud.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <wx/arrstr.h>

namespace Slic3r { namespace GUI {

ForgeCloudSettingsDialog::ForgeCloudSettingsDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Cloud & Remote Settings"),
               wxDefaultPosition, wxSize(560, 600),
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
        _L("Address of your 3DPrintForge server. It does NOT need to run on this PC — "
           "point it at the server's IP (e.g. http://192.168.1.50:3000). Drives the Fleet "
           "panel, fleet printers under \"Printer\", spool stock, and \"Send to 3DPrintForge\"."));
    hint->Wrap(500);
    root->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, 16);

    auto* url_row = new wxBoxSizer(wxHORIZONTAL);
    url_row->Add(new wxStaticText(this, wxID_ANY, _L("Server address:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    m_url = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(forge_dashboard_url()));
    m_url->SetHint("http://192.168.1.50:3000");
    url_row->Add(m_url, 1, wxALIGN_CENTER_VERTICAL);
    auto* btn_test = new wxButton(this, wxID_ANY, _L("Test"));
    url_row->Add(btn_test, 0, wxLEFT, 8);
    root->Add(url_row, 0, wxEXPAND | wxALL, 16);

    // --- Inventory (spool stock) -------------------------------------------
    // Drives the slicer's spool-aware features: "not enough filament" warning,
    // real cost on the waste badge, and real spool colours in the picker. Any
    // provider matching the documented contract works (3DPrintForge or Spoolman).
    auto* inv_heading = new wxStaticText(this, wxID_ANY, _L("Filament inventory (spool stock)"));
    auto invf = inv_heading->GetFont(); invf.MakeBold(); inv_heading->SetFont(invf);
    root->Add(inv_heading, 0, wxLEFT | wxRIGHT | wxTOP, 16);

    AppConfig* cfg = wxGetApp().app_config;
    const std::string inv_provider = cfg ? cfg->get("inventory_provider") : std::string();
    const std::string inv_url      = cfg ? cfg->get("inventory_url")      : std::string();
    const std::string inv_token    = cfg ? cfg->get("inventory_token")    : std::string();

    auto* inv_prov_row = new wxBoxSizer(wxHORIZONTAL);
    inv_prov_row->Add(new wxStaticText(this, wxID_ANY, _L("Provider:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    m_inv_provider = new wxChoice(this, wxID_ANY);
    m_inv_provider->Append(_L("Off"));                      // index 0 -> ""
    m_inv_provider->Append(_L("3DPrintForge dashboard"));   // index 1 -> "3dprintforge"
    m_inv_provider->Append(_L("Spoolman"));                 // index 2 -> "spoolman"
    m_inv_provider->SetSelection(inv_provider == "3dprintforge" ? 1 : inv_provider == "spoolman" ? 2 : 0);
    inv_prov_row->Add(m_inv_provider, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(inv_prov_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 16);

    auto* inv_url_row = new wxBoxSizer(wxHORIZONTAL);
    inv_url_row->Add(new wxStaticText(this, wxID_ANY, _L("Inventory URL:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    m_inv_url = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(inv_url));
    m_inv_url->SetToolTip(_L("Base URL of the inventory server, e.g. http://localhost:3000 (3DPrintForge) "
                            "or http://localhost:7912 (Spoolman). Leave empty to reuse the Dashboard URL above."));
    inv_url_row->Add(m_inv_url, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(inv_url_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    auto* inv_tok_row = new wxBoxSizer(wxHORIZONTAL);
    inv_tok_row->Add(new wxStaticText(this, wxID_ANY, _L("Token (optional):")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    m_inv_token = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(inv_token));
    inv_tok_row->Add(m_inv_token, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(inv_tok_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);

    auto* prov_heading = new wxStaticText(this, wxID_ANY, _L("Cloud providers"));
    auto pf = prov_heading->GetFont(); pf.MakeBold(); prov_heading->SetFont(pf);
    root->Add(prov_heading, 0, wxLEFT | wxRIGHT, 16);

    m_providers = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL);
    m_providers->InsertColumn(0, _L("Provider"), wxLIST_FORMAT_LEFT, 260);
    m_providers->InsertColumn(1, _L("Status"),   wxLIST_FORMAT_LEFT, 220);
    root->Add(m_providers, 1, wxEXPAND | wxALL, 16);

    auto* note = new wxStaticText(this, wxID_ANY,
        _L("Printers connected to your 3DPrintForge dashboard are reached through it "
           "(Bambu, Prusa, OctoPrint, Klipper/Moonraker and more) — no separate cloud accounts needed."));
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

    // Persist the inventory (spool stock) provider for the slicer's spool features.
    if (AppConfig* cfg = wxGetApp().app_config) {
        const int sel = m_inv_provider ? m_inv_provider->GetSelection() : 0;
        const std::string provider = sel == 1 ? "3dprintforge" : sel == 2 ? "spoolman" : "";

        std::string inv_url = m_inv_url ? m_inv_url->GetValue().ToStdString() : std::string();
        while (!inv_url.empty() && inv_url.back() == '/') inv_url.pop_back();
        // Convenience: a 3DPrintForge inventory with no explicit URL reuses the
        // dashboard URL the user just entered above.
        if (provider == "3dprintforge" && inv_url.empty())
            inv_url = url;

        cfg->set("inventory_provider", provider);
        cfg->set("inventory_url",      inv_url);
        cfg->set("inventory_token",    m_inv_token ? m_inv_token->GetValue().ToStdString() : std::string());
        cfg->save();
    }
    evt.Skip(); // let wxID_OK close the dialog
}

}} // namespace Slic3r::GUI

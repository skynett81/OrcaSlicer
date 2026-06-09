#include "ForgeAccountDialog.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"

#include "slic3r/Utils/ForgeCloudAgent.hpp"
#include "slic3r/GUI/ForgeCloud.hpp"
#include "libslic3r/AppConfig.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>

namespace Slic3r { namespace GUI {

ForgeAccountDialog::ForgeAccountDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Sign in to 3DPrintForge"), wxDefaultPosition,
               wxSize(440, 360), wxDEFAULT_DIALOG_STYLE)
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* intro = new wxStaticText(this, wxID_ANY,
        _L("Sign in to your own 3DPrintForge Server. It can run on another "
           "machine — enter its address below."));
    intro->Wrap(400);
    root->Add(intro, 0, wxALL, 12);

    auto* grid = new wxFlexGridSizer(2, wxSize(8, 8));
    grid->AddGrowableCol(1, 1);

    auto add_row = [&](const wxString& label, wxTextCtrl*& ctrl, long style = 0) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        ctrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, style);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    add_row(_L("Server address"), m_url);
    add_row(_L("Username"), m_user);
    add_row(_L("Password"), m_pass, wxTE_PASSWORD);

    m_totp_lbl = new wxStaticText(this, wxID_ANY, _L("2FA code"));
    grid->Add(m_totp_lbl, 0, wxALIGN_CENTER_VERTICAL);
    m_totp = new wxTextCtrl(this, wxID_ANY);
    grid->Add(m_totp, 1, wxEXPAND);

    root->Add(grid, 0, wxLEFT | wxRIGHT | wxEXPAND, 12);

    m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root->Add(m_status, 0, wxALL, 12);

    auto* btns = new wxBoxSizer(wxHORIZONTAL);
    m_login = new wxButton(this, wxID_ANY, _L("Log in"));
    auto* cancel = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    btns->AddStretchSpacer();
    btns->Add(m_login, 0, wxRIGHT, 8);
    btns->Add(cancel, 0);
    root->Add(btns, 0, wxALL | wxEXPAND, 12);

    SetSizer(root);

    // Prefill the server address from the configured dashboard URL.
    m_url->SetValue(from_u8(forge_dashboard_url()));
    show_totp(false);

    m_login->Bind(wxEVT_BUTTON, &ForgeAccountDialog::on_login, this);
    m_user->SetFocus();

    wxGetApp().UpdateDlgDarkUI(this);
}

void ForgeAccountDialog::show_totp(bool show)
{
    m_totp_lbl->Show(show);
    m_totp->Show(show);
    Layout();
}

void ForgeAccountDialog::on_login(wxCommandEvent&)
{
    const std::string url  = into_u8(m_url->GetValue()).empty() ? forge_dashboard_url()
                                                                : into_u8(m_url->GetValue());
    const std::string user = into_u8(m_user->GetValue());
    const std::string pass = into_u8(m_pass->GetValue());
    const std::string totp = into_u8(m_totp->GetValue());

    if (user.empty() || pass.empty()) {
        m_status->SetLabel(_L("Enter a username and password."));
        return;
    }

    m_login->Disable();
    m_status->SetLabel(_L("Signing in..."));
    wxYield();

    ForgeCloudAgent agent;
    agent.set_server_url(url);
    const bool ok = agent.login(user, pass, totp);
    const ForgeAuthState& st = agent.auth_state();

    m_login->Enable();

    if (ok) {
        // Persist server + session so fleet/inventory calls are authenticated.
        set_forge_dashboard_url(url);
        if (AppConfig* cfg = wxGetApp().app_config) {
            cfg->set("forge_session_token", st.session_token);
            cfg->set("forge_account_user", user);
            // Make sure the inventory provider points at this server too.
            if (cfg->get("inventory_provider").empty()) {
                cfg->set("inventory_provider", "3dprintforge");
                cfg->set("inventory_url", url);
            }
            cfg->save();
        }
        EndModal(wxID_OK);
        return;
    }

    if (st.totp_required) {
        show_totp(true);
        m_totp->SetFocus();
        m_status->SetLabel(_L("Enter the 6-digit code from your authenticator app."));
        return;
    }

    m_status->SetLabel(st.last_error.empty() ? _L("Sign in failed.") : from_u8(st.last_error));
}

}} // namespace Slic3r::GUI

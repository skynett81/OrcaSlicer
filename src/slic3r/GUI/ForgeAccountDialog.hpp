#ifndef slic3r_GUI_ForgeAccountDialog_hpp_
#define slic3r_GUI_ForgeAccountDialog_hpp_

#include <wx/dialog.h>

class wxTextCtrl;
class wxStaticText;
class wxButton;

namespace Slic3r { namespace GUI {

// Real sign-in dialog for the user's own 3DPrintForge Server. Posts to
// /api/auth/login with username + password (+ a TOTP code when the server
// asks for one) and stores the session so the slicer's fleet/inventory calls
// are authenticated. This replaces the Bambu/Orca cloud login on the home page.
class ForgeAccountDialog : public wxDialog
{
public:
    explicit ForgeAccountDialog(wxWindow* parent);

private:
    void on_login(wxCommandEvent& evt);
    void show_totp(bool show);

    wxTextCtrl*   m_url      = nullptr;
    wxTextCtrl*   m_user     = nullptr;
    wxTextCtrl*   m_pass     = nullptr;
    wxStaticText* m_totp_lbl = nullptr;
    wxTextCtrl*   m_totp     = nullptr;
    wxStaticText* m_status   = nullptr;
    wxButton*     m_login    = nullptr;
};

}} // namespace Slic3r::GUI

#endif

#ifndef slic3r_GUI_ForgeCloudSettingsDialog_hpp_
#define slic3r_GUI_ForgeCloudSettingsDialog_hpp_

#include <wx/dialog.h>

class wxTextCtrl;
class wxListCtrl;
class wxChoice;

namespace Slic3r { namespace GUI {

// "Cloud & Remote" settings — configures the 3DPrintForge dashboard URL
// (shared by the fleet panel, Forge Library and "Send to 3DPrintForge")
// and lists the available cloud providers. Phase 3 adds per-provider
// credential rows here.
class ForgeCloudSettingsDialog : public wxDialog
{
public:
    explicit ForgeCloudSettingsDialog(wxWindow* parent);

private:
    void build_ui();
    void populate_providers();
    void on_test(wxCommandEvent& evt);
    void on_save(wxCommandEvent& evt);

    wxTextCtrl* m_url        = nullptr;
    wxListCtrl* m_providers  = nullptr;
    // Inventory (spool stock) provider — drives the slicer's spool-aware features.
    wxChoice*   m_inv_provider = nullptr;
    wxTextCtrl* m_inv_url      = nullptr;
    wxTextCtrl* m_inv_token    = nullptr;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_ForgeCloudSettingsDialog_hpp_

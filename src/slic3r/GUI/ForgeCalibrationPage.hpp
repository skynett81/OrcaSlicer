#ifndef slic3r_GUI_ForgeCalibrationPage_hpp_
#define slic3r_GUI_ForgeCalibrationPage_hpp_

#include <wx/scrolwin.h>

class wxStaticText;
class wxTextCtrl;
class wxButton;

namespace Slic3r { namespace GUI {

// A page in the Calibration tab's left navigation, tailored to 3DPrintForge:
// brand-agnostic calibration memory for the whole fleet (not just connected
// Bambu printers). Documents what it does and lets the user apply/save the
// saved flow ratio / pressure advance / max volumetric speed for the active
// printer + filament.
class ForgeCalibrationPage : public wxScrolledWindow
{
public:
    explicit ForgeCalibrationPage(wxWindow* parent);

    // include_dashboard=false reads only the local cache (instant, UI-safe) and
    // is used for refresh-on-show; true also does a blocking dashboard sync and
    // is used for the explicit Refresh button.
    void refresh(bool include_dashboard = false);

private:
    void on_apply(wxCommandEvent& evt);
    void on_save(wxCommandEvent& evt);

    wxStaticText* m_context = nullptr;
    wxStaticText* m_best    = nullptr;
    wxTextCtrl*   m_list    = nullptr;
    wxButton*     m_apply   = nullptr;
};

}} // namespace Slic3r::GUI

#endif

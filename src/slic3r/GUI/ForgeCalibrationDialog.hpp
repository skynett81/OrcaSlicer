#ifndef slic3r_GUI_ForgeCalibrationDialog_hpp_
#define slic3r_GUI_ForgeCalibrationDialog_hpp_

#include <wx/dialog.h>

class wxStaticText;
class wxTextCtrl;
class wxButton;

namespace Slic3r { namespace GUI {

// 3DPrintForge "Fleet Calibration" — a brand-agnostic calibration memory.
// Shows the saved calibration (flow ratio / pressure advance / max volumetric
// speed) for the active printer + filament, lets you apply it to the current
// filament preset, and lets you save the current settings back as the
// calibration for this printer+filament. Records persist locally and sync with
// the dashboard when configured, so the whole fleet shares one memory.
class ForgeCalibrationDialog : public wxDialog
{
public:
    explicit ForgeCalibrationDialog(wxWindow* parent);

private:
    void refresh();
    void on_apply(wxCommandEvent& evt);
    void on_save(wxCommandEvent& evt);

    // Active context, resolved on construction / refresh.
    std::string m_printer;   // printer preset name (the key)
    std::string m_material;
    std::string m_vendor;
    double      m_nozzle = -1;

    wxStaticText* m_context = nullptr; // "Printer X · PLA (Bambu) · 0.4 mm"
    wxStaticText* m_best    = nullptr; // best-match summary
    wxTextCtrl*   m_list    = nullptr; // all records for this printer
    wxButton*     m_apply   = nullptr;
};

}} // namespace Slic3r::GUI

#endif

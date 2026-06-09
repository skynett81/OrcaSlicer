#ifndef slic3r_GUI_ForgeColorLayerDialog_hpp_
#define slic3r_GUI_ForgeColorLayerDialog_hpp_

#include <wx/dialog.h>

class wxTextCtrl;
class wxFilePickerCtrl;
class wxStaticText;

namespace Slic3r { namespace GUI {

// HueForge-style "Colour Layer" generator. Turns an image into a printable relief
// that reproduces it by stacking the plate's loaded filaments (a colour ramp via
// the Beer-Lambert model). On Generate it builds the mesh, adds it to the plate,
// and reports the colour-change layers to set on the layer slider.
class ForgeColorLayerDialog : public wxDialog
{
public:
    explicit ForgeColorLayerDialog(wxWindow* parent);

private:
    void on_generate(wxCommandEvent& evt);

    wxFilePickerCtrl* m_image     = nullptr;
    wxTextCtrl*       m_layers    = nullptr; // colour-ramp layers
    wxTextCtrl*       m_layer_h   = nullptr; // mm
    wxTextCtrl*       m_base      = nullptr; // solid base layers
    wxTextCtrl*       m_pixel_mm  = nullptr; // model XY mm per grid cell
    wxTextCtrl*       m_td        = nullptr; // filament transmission distance (mm)
    wxStaticText*     m_info      = nullptr;
};

}} // namespace Slic3r::GUI

#endif

#include "ForgeColorLayerDialog.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "I18N.hpp"

#include "libslic3r/ColorLayer.hpp"
#include "libslic3r/ColorLayerMesh.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/filepicker.h>
#include <wx/image.h>
#include <wx/msgdlg.h>

#include <boost/filesystem.hpp>
#include <vector>
#include <cstdint>

namespace Slic3r { namespace GUI {

static ColorLayerRGB hex_to_rgb(const std::string& hex)
{
    std::string s = hex;
    if (!s.empty() && s.front() == '#') s.erase(s.begin());
    auto hx = [&](int i) -> double {
        if (i + 1 >= (int)s.size()) return 0.0;
        auto v = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        return v(s[i]) * 16 + v(s[i + 1]);
    };
    return { hx(0), hx(2), hx(4) };
}

ForgeColorLayerDialog::ForgeColorLayerDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Colour Layer Generator (HueForge-style)"),
               wxDefaultPosition, wxSize(560, -1), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* hint = new wxStaticText(this, wxID_ANY,
        _L("Reproduce an image as a printable relief using the filaments loaded on the "
           "current plate (dark to light, in order). Set those filaments first, then "
           "Generate — the model is added to the plate and the colour-change layers are "
           "listed for you to set on the layer slider."));
    hint->Wrap(520);
    root->Add(hint, 0, wxALL, 12);

    auto* grid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
    grid->AddGrowableCol(1, 1);
    auto row = [&](const wxString& label, wxWindow* ctrl) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_image = new wxFilePickerCtrl(this, wxID_ANY, "", _L("Choose an image"),
                                   "Images (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp",
                                   wxDefaultPosition, wxDefaultSize, wxFLP_OPEN | wxFLP_USE_TEXTCTRL);
    m_layers   = new wxTextCtrl(this, wxID_ANY, "16");
    m_layer_h  = new wxTextCtrl(this, wxID_ANY, "0.10");
    m_base     = new wxTextCtrl(this, wxID_ANY, "3");
    m_pixel_mm = new wxTextCtrl(this, wxID_ANY, "0.4");
    m_td       = new wxTextCtrl(this, wxID_ANY, "0.4");

    row(_L("Image:"),               m_image);
    row(_L("Colour-ramp layers:"),  m_layers);
    row(_L("Layer height (mm):"),   m_layer_h);
    row(_L("Base layers:"),         m_base);
    row(_L("Size per pixel (mm):"), m_pixel_mm);
    row(_L("Filament TD (mm):"),    m_td);
    root->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    m_info = new wxStaticText(this, wxID_ANY, "");
    m_info->Wrap(520);
    root->Add(m_info, 0, wxALL, 12);

    auto* btns = new wxBoxSizer(wxHORIZONTAL);
    auto* gen = new wxButton(this, wxID_ANY, _L("Generate"));
    btns->AddStretchSpacer();
    btns->Add(gen, 0, wxRIGHT, 8);
    btns->Add(new wxButton(this, wxID_CANCEL, _L("Close")), 0);
    root->Add(btns, 0, wxEXPAND | wxALL, 12);

    SetSizerAndFit(root);
    gen->Bind(wxEVT_BUTTON, &ForgeColorLayerDialog::on_generate, this);
    wxGetApp().UpdateDlgDarkUI(this);
    CentreOnParent();
}

void ForgeColorLayerDialog::on_generate(wxCommandEvent& /*evt*/)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;

    const wxString path = m_image->GetPath();
    if (path.empty()) {
        wxMessageBox(_L("Choose an image first."), _L("Colour Layer"), wxICON_INFORMATION, this);
        return;
    }

    // Filaments loaded on the plate become the colour ramp (in order).
    std::vector<std::string> hexes = plater->get_extruder_colors_from_plater_config();
    if (hexes.size() < 2) {
        wxMessageBox(_L("Load at least two filaments on the plate (dark to light) first."),
                     _L("Colour Layer"), wxICON_INFORMATION, this);
        return;
    }

    auto to_d = [](wxTextCtrl* c, double dflt) {
        double v = dflt; if (c) c->GetValue().ToDouble(&v); return v;
    };
    const int    layers     = std::max(2, (int)to_d(m_layers, 16));
    const double layer_h    = to_d(m_layer_h, 0.10);
    const int    base_lay   = std::max(0, (int)to_d(m_base, 3));
    const double pixel_mm   = to_d(m_pixel_mm, 0.4);
    const double td         = to_d(m_td, 0.4);
    if (layer_h <= 0.0 || pixel_mm <= 0.0) {
        wxMessageBox(_L("Layer height and size per pixel must be positive."),
                     _L("Colour Layer"), wxICON_ERROR, this);
        return;
    }

    // Decode the image to RGBA.
    wxImage img;
    if (!img.LoadFile(path) || !img.IsOk()) {
        wxMessageBox(_L("Could not read that image."), _L("Colour Layer"), wxICON_ERROR, this);
        return;
    }
    const int iw = img.GetWidth(), ih = img.GetHeight();
    const unsigned char* rgb = img.GetData();
    std::vector<uint8_t> rgba((size_t)iw * ih * 4, 255);
    for (int i = 0; i < iw * ih; ++i) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0];
        rgba[i * 4 + 1] = rgb[i * 3 + 1];
        rgba[i * 4 + 2] = rgb[i * 3 + 2];
    }

    int gw = 0, gh = 0;
    std::vector<ColorLayerRGB> grid = downsample_to_color_grid(rgba.data(), iw, ih, 200, gw, gh);
    if (grid.empty() || gw < 2 || gh < 2) {
        wxMessageBox(_L("Image too small after downsampling."), _L("Colour Layer"), wxICON_ERROR, this);
        return;
    }

    // Build the per-layer filament schedule: each filament gets an even band.
    ColorLayerParams params;
    params.base            = hex_to_rgb(hexes.front());
    params.layer_height_mm = (float)layer_h;
    params.pixel_mm        = (float)pixel_mm;
    params.base_layers     = base_lay;
    for (int i = 0; i < layers; ++i) {
        const int fi = (int)((long long)i * (long long)hexes.size() / layers);
        const ColorLayerRGB c = hex_to_rgb(hexes[std::min((size_t)fi, hexes.size() - 1)]);
        ColorLayerFilament f;
        f.r = (uint8_t)c.r; f.g = (uint8_t)c.g; f.b = (uint8_t)c.b; f.td_mm = td;
        params.layer_schedule.push_back(f);
    }

    ColorLayerResult res = generate_color_layer(grid, gw, gh, params);
    if (!res.ok) {
        wxMessageBox(_L("Could not generate the relief."), _L("Colour Layer"), wxICON_ERROR, this);
        return;
    }

    // Write the mesh to a temp STL and load it into the plate (proven import path).
    namespace fs = boost::filesystem;
    fs::path out = fs::temp_directory_path() / "forge_colour_layer.stl";
    if (!its_write_stl_binary(out.string().c_str(), "ColourLayer", res.mesh)) {
        wxMessageBox(_L("Could not export the generated mesh."), _L("Colour Layer"), wxICON_ERROR, this);
        return;
    }
    wxString out_wx = wxString::FromUTF8(out.string());
    plater->load_files(wxArrayString{ 1, &out_wx });

    // Report the colour-change layers (set these on the layer slider).
    wxString msg = wxString::Format(_L("Added a %d x %d relief (%d colour-ramp layers).\n\n"), gw, gh, layers);
    if (res.color_change_layers.empty()) {
        msg += _L("No colour changes (single filament band).");
    } else {
        msg += _L("Set these colour changes on the layer slider:\n");
        for (size_t i = 0; i < res.color_change_layers.size(); ++i) {
            const int    layer = res.color_change_layers[i];
            const double z     = layer * layer_h;
            msg += wxString::Format(_L("  • layer %d  (Z = %.2f mm)  -> filament %zu\n"),
                                    layer, z, i + 2);
        }
    }
    m_info->SetLabel(msg);
    m_info->Wrap(520);
    Layout();
    Fit();
}

}} // namespace Slic3r::GUI

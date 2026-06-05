#include "ForgeLibraryDialog.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"

#include "libslic3r_version.h"

#include <wx/arrstr.h>
#include <wx/button.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <chrono>

namespace Slic3r { namespace GUI {

namespace {
// 3DPrintForge dashboard URL — keep configurable via env so a developer
// running the dashboard on a non-default port can still launch the
// right page from the slicer.
std::string dashboard_url() {
    if (const char* env = std::getenv("FORGE_DASHBOARD_URL"); env && *env) return env;
    return "https://localhost:3443";
}

// First-iteration static catalog. Mirrors server/generators/ in
// 3DPrintForge. Subsequent commits will replace this with a dynamic
// fetch from GET /api/ai-forge/generators so new generators show up
// automatically.
struct GeneratorEntry {
    const char* id;     // url-path slug under /api/model-forge/
    const char* label;
    const char* description;
};
constexpr GeneratorEntry kGenerators[] = {
    {"gridfinity-baseplate", "Gridfinity Baseplate",      "Modular baseplate (U × V cells)"},
    {"gridfinity-bin",       "Gridfinity Bin",            "Modular bin with optional dividers"},
    {"gridfinity-lid",       "Gridfinity Lid",            "Snap-on lid for any Gridfinity bin"},
    {"gridfinity-tool-holder","Gridfinity Tool Holder",   "Bored holes for tools/markers/etc."},
    {"gear",                 "Spur Gear",                 "Parametric involute gear"},
    {"pulley",               "GT2/GT3 Pulley",            "Timing-belt pulley with bore"},
    {"spring",               "Helical Spring",            "Printable compression spring"},
    {"hinge",                "Print-in-Place Hinge",      "No-assembly hinge"},
    {"snapfit",              "Snap-fit Connector",        "Square snap-fit lug/socket pair"},
    {"spool-adapter",        "Spool Adapter",             "Hub adapter for non-standard spools"},
    {"cable-chain",          "Cable Chain Link",          "Drag-chain segment"},
    {"first-layer",          "First-Layer Test",          "Calibration patch with letter labels"},
    {"nozzle-storage",       "Nozzle Storage",            "Storage tray for spare nozzles"},
    {"scraper-holder",       "Scraper/Tool Holder",       "Wall holder for hand tools"},
    {"hook",                 "Wall Hook",                 "Generic hook with mount tab"},
    {"cable-clip",           "Cable Clip",                "Adhesive cable clip"},
    {"plant-pot",            "Plant Pot",                 "Drainage pot with saucer"},
    {"desk-organizer",       "Desk Organizer",            "Multi-compartment desk tray"},
    {"wall-bracket",         "Wall Bracket",              "Adjustable L-bracket"},
    {"wall-plate",           "Wall Plate",                "Flat wall plate with holes"},
    {"lithophane",           "Lithophane",                "Image -> thin lithophane"},
    {"text-plate",           "Text Plate",                "Embossed/debossed text sign"},
    {"keychain",             "Keychain",                  "Text/icon keychain"},
    {"cable-label",          "Cable Label",               "Wrap-around cable label"},
    {"calibration",          "Calibration Tools",         "Temp tower, retraction, flow, etc."},
    {"lattice",              "Lattice Structure",         "Repeating lattice cell"},
    {"multi-color",          "Multi-Color Sample",        "Test piece for multi-material"},
    {"vase",                 "Advanced Vase",             "Profile-driven vase"},
    {"thread",               "Threads & Joints",          "Metric/imperial threaded parts"},
    {"texture",              "Texture Surface",           "Repeating surface texture"},
    {"threemf-converter",    "3MF Converter",             "STL/OBJ -> 3MF with metadata"},
    {"threemf-validator",    "3MF Validator",             "Inspect/repair existing 3MF"},
    {"stencil",              "Stencil",                   "Cut-out stencil from image"},
    {"relief",               "Relief Map",                "Heightmap relief from image"},
    {"qr3d",                 "QR Code (3D)",              "Scannable 3D QR plaque"},
    {"topo-map",             "Topographic Map",           "Terrain heightmap"},
    {"voronoi-tray",         "Voronoi Tray",              "Tray with Voronoi infill walls"},
    {"honeycomb-tile",       "Honeycomb Tile",            "Hex tile assembly"},
    {"shape-extruder",       "Shape Extruder",            "Profile -> swept solid"},
    {"jscad-generator",      "JSCAD Script",              "Custom JSCAD model"},
    {"battery-holder",       "Battery Holder",            "Slots for AA/AAA/18650/etc."},
    {"phone-stand",          "Phone Stand",               "Adjustable phone/tablet stand"},
    {"headphone-stand",      "Headphone Stand",           "Desktop headphone hanger"},
    {"vesa-mount",           "VESA Mount Adapter",        "75/100 mm VESA plate"},
    {"electronics-case",     "Electronics Case",          "Generic box for boards/modules"},
    {"storage-box",          "Storage Box",               "Lidded storage box"},
    {"lidded-box",           "Lidded Box",                "Friction-fit lidded box"},
    {"mini-base",            "Miniature Base",            "Round/oval/square mini base"},
    {"peg-rail",             "Peg Rail",                  "French-cleat peg rail"},
    {"dice-tower",           "Dice Tower",                "Tabletop dice tower"},
};

} // namespace

ForgeLibraryDialog::ForgeLibraryDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY,
               wxString::Format(_L("%s — Forge Library"), SLIC3R_APP_NAME),
               wxDefaultPosition, wxSize(560, 480),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetBackgroundColour(*wxWHITE);

    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, _L("Parametric Model Generators"));
    auto title_font = title->GetFont();
    title_font.SetPointSize(title_font.GetPointSize() + 2);
    title_font.MakeBold();
    title->SetFont(title_font);
    root->Add(title, 0, wxALL, 12);

    m_hint = new wxStaticText(this, wxID_ANY,
        _L("Pick a generator and open it in the 3DPrintForge dashboard. "
           "A future build will run generation directly inside the slicer "
           "and drop the result onto the active plate."));
    m_hint->Wrap(520);
    root->Add(m_hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    wxArrayString items;
    for (const auto& g : kGenerators) {
        items.Add(wxString::Format("%s — %s", g.label, g.description));
    }
    m_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, items);
    if (!items.IsEmpty()) m_list->SetSelection(0);
    root->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    m_btn_generate = new wxButton(this, wxID_ANY, _L("Generate with defaults"));
    m_btn_open = new wxButton(this, wxID_OPEN, _L("Open in 3DPrintForge"));
    auto* close = new wxButton(this, wxID_CLOSE, _L("Close"));
    buttons->Add(m_btn_generate, 0, wxRIGHT, 8);
    buttons->Add(m_btn_open, 0, wxRIGHT, 8);
    buttons->AddStretchSpacer(1);
    buttons->Add(close, 0);
    root->Add(buttons, 0, wxEXPAND | wxALL, 12);

    SetSizer(root);

    m_btn_generate->Bind(wxEVT_BUTTON, &ForgeLibraryDialog::on_generate_default, this);
    m_btn_open->Bind(wxEVT_BUTTON, &ForgeLibraryDialog::on_open_in_browser, this);
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); });

    CentreOnParent();
}

void ForgeLibraryDialog::on_generate_default(wxCommandEvent& /*evt*/)
{
    int idx = m_list->GetSelection();
    if (idx < 0 || idx >= static_cast<int>(std::size(kGenerators))) return;
    const auto& g = kGenerators[idx];

    // Drop the generated 3MF in the system temp dir under a generator-
    // specific filename so successive runs don't clobber each other.
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    wxFileName tmp(wxStandardPaths::Get().GetTempDir(),
                   wxString::Format("forge-%s-%lld.3mf", g.id, static_cast<long long>(ts)));
    const wxString out_path = tmp.GetFullPath();

    const wxString url = wxString::Format(
        "%s/api/model-forge/%s/generate-3mf", dashboard_url(), g.id);
    // curl is bridged via wxExecute so we don't pull in OpenSSL just to
    // call our own dashboard. Phase 3 will replace this with cpp-httplib
    // + OpenSSL once we wire that into the build. Empty JSON body uses
    // each generator's documented defaults.
    wxString cmd = wxString::Format(
        "curl -ks -X POST %s -H \"Content-Type: application/json\" "
        "-d \"{}\" --max-time 60 -o %s",
        url, out_path);

    {
        wxBusyCursor wait;
        wxArrayString out;
        wxArrayString err;
        long exit_code = wxExecute(cmd, out, err, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE);
        if (exit_code != 0) {
            wxMessageBox(
                wxString::Format(_L("Failed to generate %s. curl exit code: %ld\n\n"
                                    "Is 3DPrintForge running at %s?"),
                                 g.label, exit_code, dashboard_url()),
                _L("Generation failed"), wxOK | wxICON_ERROR, this);
            return;
        }
    }

    // Sanity-check the file before handing it to the slicer pipeline —
    // an empty 3MF means the server responded but produced no output.
    if (!tmp.FileExists() || tmp.GetSize() == 0) {
        wxMessageBox(_L("Generator returned an empty file. Check the 3DPrintForge log."),
                     _L("Generation failed"), wxOK | wxICON_ERROR, this);
        return;
    }

    // Loading replaces no existing plate items — we hand the path to
    // Plater::load_files which appends the model to the current scene.
    Plater* plater = wxGetApp().plater();
    if (plater) {
        plater->load_files(wxArrayString{1, &out_path});
    }
    EndModal(wxID_OK);
}

void ForgeLibraryDialog::on_open_in_browser(wxCommandEvent& /*evt*/)
{
    int idx = m_list->GetSelection();
    if (idx < 0 || idx >= static_cast<int>(std::size(kGenerators))) return;
    const auto& g = kGenerators[idx];
    // 3DPrintForge's Model Forge tab takes ?generator=<id> as a hash
    // query and pre-selects the generator on load.
    wxString url = wxString::Format("%s/#model-forge?generator=%s",
                                    dashboard_url(), g.id);
    wxLaunchDefaultBrowser(url, wxBROWSER_NEW_WINDOW);
}

}} // namespace Slic3r::GUI

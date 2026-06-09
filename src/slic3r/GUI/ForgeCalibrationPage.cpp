#include "ForgeCalibrationPage.hpp"
#include "ForgeCalibrationApply.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"

#include "slic3r/Utils/ForgeCalibrationProvider.hpp"
#include "slic3r/Utils/InventoryProvider.hpp"
#include "libslic3r/ForgeCalibration.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/font.h>

#include <algorithm>
#include <cctype>

#include <sstream>
#include <iomanip>

namespace Slic3r { namespace GUI {

static wxStaticText* heading(wxWindow* parent, const wxString& text, int pt_delta, bool bold)
{
    auto* t = new wxStaticText(parent, wxID_ANY, text);
    wxFont f = t->GetFont();
    f.SetPointSize(f.GetPointSize() + pt_delta);
    if (bold) f.SetWeight(wxFONTWEIGHT_BOLD);
    t->SetFont(f);
    return t;
}

ForgeCalibrationPage::ForgeCalibrationPage(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY)
{
    SetScrollRate(0, 12);
    SetBackgroundColour(*wxWHITE);

    auto* root = new wxBoxSizer(wxVERTICAL);
    const int side = 24;

    root->Add(heading(this, _L("Fleet Calibration"), 6, true), 0, wxLEFT | wxRIGHT | wxTOP, side);

    auto* lead = new wxStaticText(this, wxID_ANY,
        _L("A calibration memory for your whole 3DPrintForge fleet. Unlike the "
           "Bambu-only wizards, this works for every printer brand — Bambu, "
           "Snapmaker, Klipper/Moonraker, Prusa — and remembers the tuned values "
           "per printer and filament."));
    lead->Wrap(640);
    root->Add(lead, 0, wxLEFT | wxRIGHT | wxTOP, side);

    // --- Documentation sections ---------------------------------------------
    auto doc = [&](const wxString& title, const wxString& body) {
        root->Add(heading(this, title, 1, true), 0, wxLEFT | wxRIGHT | wxTOP, side);
        auto* b = new wxStaticText(this, wxID_ANY, body);
        b->Wrap(640);
        root->Add(b, 0, wxLEFT | wxRIGHT | wxTOP, side);
    };
    doc(_L("When do you need this"),
        _L("After you dial in flow ratio, pressure advance or max volumetric speed "
           "for a filament on a given printer, save it here so you never lose it. "
           "Next time you select that printer and filament, apply it in one click."));
    doc(_L("How it works"),
        _L("Values are stored per printer + filament (optionally per spool). They "
           "are kept on this machine and, when you sign in to a 3DPrintForge Server, "
           "synced across your fleet so every workstation shares the same memory."));
    doc(_L("About the values"),
        _L("Flow ratio corrects extrusion width, pressure advance sharpens corners "
           "and seams, and max volumetric speed caps how fast filament can be pushed. "
           "Apply writes them onto the current filament preset; Save captures the "
           "preset's current values as the calibration for this printer + filament."));

    root->AddSpacer(12);

    // --- Live context + records ---------------------------------------------
    m_context = heading(this, wxEmptyString, 1, true);
    root->Add(m_context, 0, wxLEFT | wxRIGHT | wxTOP, side);

    m_best = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_best->Wrap(640);
    root->Add(m_best, 0, wxLEFT | wxRIGHT | wxTOP, side);

    auto* list_lbl = new wxStaticText(this, wxID_ANY, _L("All saved calibrations for this printer:"));
    root->Add(list_lbl, 0, wxLEFT | wxRIGHT | wxTOP, side);
    m_list = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 120),
                            wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    root->Add(m_list, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, side);

    // Per-spool: tie the calibration to a physical spool from inventory, so it
    // follows that spool to whatever printer it is mounted on. Populated on
    // Refresh (needs the inventory server).
    auto* spool_row = new wxBoxSizer(wxHORIZONTAL);
    spool_row->Add(new wxStaticText(this, wxID_ANY, _L("Tie to spool:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    m_spool_choice = new wxChoice(this, wxID_ANY);
    spool_row->Add(m_spool_choice, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(spool_row, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, side);
    m_spool_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { this->refresh(false); });

    auto* btns = new wxBoxSizer(wxHORIZONTAL);
    m_apply = new wxButton(this, wxID_ANY, _L("Apply to filament preset"));
    auto* save = new wxButton(this, wxID_ANY, _L("Save current settings"));
    auto* refresh_btn = new wxButton(this, wxID_ANY, _L("Refresh"));
    btns->Add(m_apply, 0, wxRIGHT, 8);
    btns->Add(save, 0, wxRIGHT, 8);
    btns->Add(refresh_btn, 0);
    root->Add(btns, 0, wxALL, side);

    SetSizer(root);

    m_apply->Bind(wxEVT_BUTTON, &ForgeCalibrationPage::on_apply, this);
    save->Bind(wxEVT_BUTTON, &ForgeCalibrationPage::on_save, this);
    refresh_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { this->refresh(/*include_dashboard=*/true); });

    // Auto-refresh (local cache only — no blocking network) whenever the page is
    // shown, e.g. when the user switches to it in the Calibration tab.
    Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
        if (e.IsShown())
            this->refresh(/*include_dashboard=*/false);
        e.Skip();
    });

    refresh(/*include_dashboard=*/false);
}

void ForgeCalibrationPage::refresh(bool include_dashboard)
{
    const ForgeCaliContext ctx = forge_current_calibration_context();

    std::string c = ctx.printer.empty() ? std::string("(no printer)") : ctx.printer;
    c += "   ·   " + (ctx.material.empty() ? std::string("(no filament)") : ctx.material);
    if (!ctx.vendor.empty()) c += " (" + ctx.vendor + ")";
    if (ctx.nozzle > 0) {
        std::ostringstream n; n << std::fixed << std::setprecision(1) << ctx.nozzle;
        c += "   ·   " + n.str() + " mm";
    }
    m_context->SetLabel(_L("Active: ") + c);

    // Refresh the inventory spool list only on an explicit (network) refresh.
    if (include_dashboard) {
        const InventoryConfig icfg = inventory_config();
        m_spools = icfg.configured() ? fetch_inventory_spools(icfg) : std::vector<ForgeSpool>{};
    }
    populate_spool_choice(ctx.material);
    const int spool = selected_spool_id();

    const std::vector<ForgeCalibrationRecord> records =
        include_dashboard ? load_calibration_records() : load_cached_calibration_records();
    const int best = find_best_calibration(records, ctx.printer, spool, ctx.material, ctx.vendor, ctx.nozzle);
    if (best >= 0) {
        const bool per_spool = spool >= 0 && records[best].spool_id == spool;
        m_best->SetLabel((per_spool ? _L("Best match (this spool): ") : _L("Best match: "))
                         + forge_calibration_summary(records[best]));
        m_apply->Enable(records[best].has_any());
    } else {
        m_best->SetLabel(_L("No saved calibration for this printer + filament yet — "
                            "tune it, then 'Save current settings'."));
        m_apply->Enable(false);
    }

    std::ostringstream ss;
    int shown = 0;
    for (const auto& r : records) {
        // This printer's records, plus any record bound to the selected spool
        // (which travels across printers).
        const bool for_this_printer = r.printer_id == ctx.printer;
        const bool for_this_spool   = spool >= 0 && r.spool_id == spool;
        if (!for_this_printer && !for_this_spool) continue;
        ss << r.material;
        if (!r.vendor.empty()) ss << " (" << r.vendor << ")";
        if (r.nozzle_mm > 0) { ss << " " << std::fixed << std::setprecision(1) << r.nozzle_mm << "mm"; }
        if (r.spool_id >= 0) ss << " [spool " << r.spool_id << "]";
        ss << "  —  " << forge_calibration_summary(r);
        if (!r.updated_at.empty()) ss << "  (" << r.updated_at << ")";
        ss << "\n";
        ++shown;
    }
    if (shown == 0) ss << "(none)";
    m_list->SetValue(ss.str());

    Layout();
    FitInside();
}

void ForgeCalibrationPage::on_apply(wxCommandEvent&)
{
    const ForgeCaliContext ctx = forge_current_calibration_context();
    const std::vector<ForgeCalibrationRecord> records = load_cached_calibration_records();
    const int best = find_best_calibration(records, ctx.printer, selected_spool_id(), ctx.material, ctx.vendor, ctx.nozzle);
    if (best < 0 || !records[best].has_any()) {
        wxMessageBox(_L("No saved calibration to apply."), _L("Fleet Calibration"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    if (forge_apply_calibration(records[best]))
        wxMessageBox(_L("Applied to the filament preset: ") + forge_calibration_summary(records[best]),
                     _L("Fleet Calibration"), wxOK | wxICON_INFORMATION, this);
    else
        wxMessageBox(_L("Could not access the filament settings."), _L("Fleet Calibration"), wxOK | wxICON_ERROR, this);
}

void ForgeCalibrationPage::on_save(wxCommandEvent&)
{
    const ForgeCaliContext ctx = forge_current_calibration_context();
    if (ctx.printer.empty() || ctx.material.empty()) {
        wxMessageBox(_L("Select a printer and filament first."), _L("Fleet Calibration"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    ForgeCalibrationRecord rec = forge_capture_current_calibration(ctx);
    if (!rec.has_any()) {
        wxMessageBox(_L("The current filament has no flow ratio, pressure advance or "
                        "max volumetric speed set to save."),
                     _L("Fleet Calibration"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    // Per-spool: bind the calibration to the chosen physical spool, if any.
    rec.spool_id = selected_spool_id();

    const bool ok = save_calibration_record(rec);
    refresh();
    wxString what = (rec.spool_id >= 0) ? _L("Saved for this spool: ") : _L("Saved: ");
    wxMessageBox(ok ? what + forge_calibration_summary(rec)
                    : _L("Could not write the calibration cache."),
                 _L("Fleet Calibration"), wxOK | (ok ? wxICON_INFORMATION : wxICON_ERROR), this);
}

void ForgeCalibrationPage::populate_spool_choice(const std::string& material)
{
    if (m_spool_choice == nullptr)
        return;
    const int prev = selected_spool_id();

    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    m_spool_choice->Clear();
    m_choice_spool_ids.clear();
    m_spool_choice->Append(_L("Whole filament (no specific spool)"));
    m_choice_spool_ids.push_back(-1);

    for (const ForgeSpool& s : m_spools) {
        if (s.archived)
            continue;
        if (!material.empty() && !s.material.empty() && lower(s.material) != lower(material))
            continue;
        wxString label = s.material;
        if (!s.color_name.empty()) label += " " + s.color_name;
        if (s.remaining_g >= 0)    label += wxString::Format(" (%.0f g)", s.remaining_g);
        if (!s.location.empty())   label += " @" + s.location;
        m_spool_choice->Append(label);
        m_choice_spool_ids.push_back(s.id);
    }

    int sel = 0;
    for (size_t i = 0; i < m_choice_spool_ids.size(); ++i)
        if (m_choice_spool_ids[i] == prev) { sel = static_cast<int>(i); break; }
    m_spool_choice->SetSelection(sel);
}

int ForgeCalibrationPage::selected_spool_id() const
{
    if (m_spool_choice == nullptr)
        return -1;
    const int i = m_spool_choice->GetSelection();
    if (i < 0 || i >= static_cast<int>(m_choice_spool_ids.size()))
        return -1;
    return m_choice_spool_ids[i];
}

}} // namespace Slic3r::GUI

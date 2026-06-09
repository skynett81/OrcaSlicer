#include "ForgeCalibrationDialog.hpp"
#include "GUI_App.hpp"
#include "Tab.hpp"
#include "I18N.hpp"

#include "slic3r/Utils/ForgeCalibrationProvider.hpp"
#include "libslic3r/ForgeCalibration.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/datetime.h>

#include <sstream>
#include <iomanip>

namespace Slic3r { namespace GUI {

static std::string fmt_num(double v, int prec)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v;
    return ss.str();
}

static std::string summarize(const ForgeCalibrationRecord& r)
{
    std::ostringstream ss;
    bool first = true;
    auto add = [&](const std::string& s) { if (!first) ss << "   "; ss << s; first = false; };
    if (r.has_flow()) add("Flow " + fmt_num(r.flow_ratio, 3));
    if (r.has_pa())   add("PA " + fmt_num(r.pressure_advance, 4));
    if (r.has_mvs())  add("Max vol " + fmt_num(r.max_volumetric_speed, 1) + " mm3/s");
    if (first) ss << "(no values)";
    return ss.str();
}

ForgeCalibrationDialog::ForgeCalibrationDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Fleet Calibration"), wxDefaultPosition,
               wxSize(560, 460), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* intro = new wxStaticText(this, wxID_ANY,
        _L("Saved calibration for the active printer and filament. Works for any "
           "printer brand and (when configured) syncs with your 3DPrintForge fleet."));
    intro->Wrap(520);
    root->Add(intro, 0, wxALL, 12);

    m_context = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root->Add(m_context, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    m_best = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root->Add(m_best, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    auto* list_lbl = new wxStaticText(this, wxID_ANY, _L("All saved calibrations for this printer:"));
    root->Add(list_lbl, 0, wxLEFT | wxRIGHT, 12);
    m_list = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                            wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    root->Add(m_list, 1, wxALL | wxEXPAND, 12);

    auto* btns = new wxBoxSizer(wxHORIZONTAL);
    m_apply = new wxButton(this, wxID_ANY, _L("Apply to filament preset"));
    auto* save = new wxButton(this, wxID_ANY, _L("Save current settings"));
    auto* close = new wxButton(this, wxID_CANCEL, _L("Close"));
    btns->Add(m_apply, 0, wxRIGHT, 8);
    btns->Add(save, 0, wxRIGHT, 8);
    btns->AddStretchSpacer();
    btns->Add(close, 0);
    root->Add(btns, 0, wxALL | wxEXPAND, 12);

    SetSizer(root);

    m_apply->Bind(wxEVT_BUTTON, &ForgeCalibrationDialog::on_apply, this);
    save->Bind(wxEVT_BUTTON, &ForgeCalibrationDialog::on_save, this);

    refresh();
    wxGetApp().UpdateDlgDarkUI(this);
}

void ForgeCalibrationDialog::refresh()
{
    m_printer.clear();
    m_material.clear();
    m_vendor.clear();
    m_nozzle = -1;

    if (PresetBundle* pb = wxGetApp().preset_bundle) {
        m_printer = pb->printers.get_edited_preset().name;
        const DynamicPrintConfig& fcfg = pb->filaments.get_edited_preset().config;
        if (fcfg.has("filament_type"))   m_material = fcfg.opt_string("filament_type", 0);
        if (fcfg.has("filament_vendor")) m_vendor   = fcfg.opt_string("filament_vendor", 0);
        const DynamicPrintConfig& pcfg = pb->printers.get_edited_preset().config;
        if (pcfg.has("nozzle_diameter")) m_nozzle = pcfg.opt_float("nozzle_diameter", 0);
    }

    std::string ctx = m_printer.empty() ? std::string("(no printer)") : m_printer;
    ctx += "   ·   " + (m_material.empty() ? std::string("(no filament)") : m_material);
    if (!m_vendor.empty()) ctx += " (" + m_vendor + ")";
    if (m_nozzle > 0)      ctx += "   ·   " + fmt_num(m_nozzle, 1) + " mm";
    m_context->SetLabel(ctx);

    const std::vector<ForgeCalibrationRecord> records = load_calibration_records();
    const int best = find_best_calibration(records, m_printer, -1, m_material, m_vendor, m_nozzle);

    if (best >= 0) {
        m_best->SetLabel(_L("Best match: ") + summarize(records[best]));
        m_apply->Enable(records[best].has_any());
    } else {
        m_best->SetLabel(_L("No saved calibration for this printer + filament yet — "
                            "calibrate, then 'Save current settings'."));
        m_apply->Enable(false);
    }

    std::ostringstream ss;
    int shown = 0;
    for (const auto& r : records) {
        if (r.printer_id != m_printer) continue;
        ss << r.material;
        if (!r.vendor.empty())   ss << " (" << r.vendor << ")";
        if (r.nozzle_mm > 0)     ss << " " << fmt_num(r.nozzle_mm, 1) << "mm";
        if (r.spool_id >= 0)     ss << " [spool " << r.spool_id << "]";
        ss << "  —  " << summarize(r);
        if (!r.updated_at.empty()) ss << "  (" << r.updated_at << ")";
        ss << "\n";
        ++shown;
    }
    if (shown == 0) ss << "(none)";
    m_list->SetValue(ss.str());
}

void ForgeCalibrationDialog::on_apply(wxCommandEvent&)
{
    const std::vector<ForgeCalibrationRecord> records = load_calibration_records();
    const int best = find_best_calibration(records, m_printer, -1, m_material, m_vendor, m_nozzle);
    if (best < 0 || !records[best].has_any()) {
        wxMessageBox(_L("No saved calibration to apply."), _L("Fleet Calibration"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    const ForgeCalibrationRecord& r = records[best];

    Tab* tab = wxGetApp().get_tab(Preset::TYPE_FILAMENT);
    if (tab == nullptr || tab->get_config() == nullptr) {
        wxMessageBox(_L("Could not access the filament settings."), _L("Fleet Calibration"), wxOK | wxICON_ERROR, this);
        return;
    }
    DynamicPrintConfig new_conf = *tab->get_config();
    if (r.has_flow()) {
        std::vector<double> v{ r.flow_ratio };
        new_conf.set_key_value("filament_flow_ratio", new ConfigOptionFloatsNullable{ v });
    }
    if (r.has_pa())
        new_conf.set_key_value("pressure_advance", new ConfigOptionFloats{ r.pressure_advance });
    if (r.has_mvs())
        new_conf.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{ r.max_volumetric_speed });
    tab->load_config(new_conf);
    tab->update_dirty();

    wxMessageBox(_L("Applied to the filament preset: ") + summarize(r),
                 _L("Fleet Calibration"), wxOK | wxICON_INFORMATION, this);
}

void ForgeCalibrationDialog::on_save(wxCommandEvent&)
{
    if (m_printer.empty() || m_material.empty()) {
        wxMessageBox(_L("Select a printer and filament first."), _L("Fleet Calibration"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    ForgeCalibrationRecord rec;
    rec.printer_id = m_printer;
    rec.material   = m_material;
    rec.vendor     = m_vendor;
    rec.nozzle_mm  = m_nozzle;
    rec.source     = "manual";
    rec.updated_at = wxDateTime::Now().FormatISODate().ToStdString();

    if (PresetBundle* pb = wxGetApp().preset_bundle) {
        const DynamicPrintConfig& fcfg = pb->filaments.get_edited_preset().config;
        if (auto* o = fcfg.option<ConfigOptionFloatsNullable>("filament_flow_ratio"); o && !o->values.empty()) {
            double v = o->values[0];
            if (v > 0.0 && v < 5.0) rec.flow_ratio = v;
        }
        if (auto* o = fcfg.option<ConfigOptionFloats>("pressure_advance"); o && !o->values.empty()) {
            double v = o->values[0];
            if (v >= 0.0) rec.pressure_advance = v;
        }
        if (auto* o = fcfg.option<ConfigOptionFloats>("filament_max_volumetric_speed"); o && !o->values.empty()) {
            double v = o->values[0];
            if (v > 0.0) rec.max_volumetric_speed = v;
        }
    }

    if (!rec.has_any()) {
        wxMessageBox(_L("The current filament has no flow ratio, pressure advance or "
                        "max volumetric speed set to save."),
                     _L("Fleet Calibration"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    const bool ok = save_calibration_record(rec);
    refresh();
    wxMessageBox(ok ? _L("Saved: ") + summarize(rec)
                    : _L("Could not write the calibration cache."),
                 _L("Fleet Calibration"), wxOK | (ok ? wxICON_INFORMATION : wxICON_ERROR), this);
}

}} // namespace Slic3r::GUI

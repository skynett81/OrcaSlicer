#include "ForgeCalibrationApply.hpp"
#include "GUI_App.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

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

std::string forge_calibration_summary(const ForgeCalibrationRecord& rec)
{
    std::ostringstream ss;
    bool first = true;
    auto add = [&](const std::string& s) { if (!first) ss << "   "; ss << s; first = false; };
    if (rec.has_flow()) add("Flow " + fmt_num(rec.flow_ratio, 3));
    if (rec.has_pa())   add("PA " + fmt_num(rec.pressure_advance, 4));
    if (rec.has_mvs())  add("Max vol " + fmt_num(rec.max_volumetric_speed, 1) + " mm3/s");
    if (first) ss << "(no values)";
    return ss.str();
}

ForgeCaliContext forge_current_calibration_context()
{
    ForgeCaliContext ctx;
    if (PresetBundle* pb = wxGetApp().preset_bundle) {
        ctx.printer = pb->printers.get_edited_preset().name;
        const DynamicPrintConfig& fcfg = pb->filaments.get_edited_preset().config;
        if (fcfg.has("filament_type"))   ctx.material = fcfg.opt_string("filament_type", 0);
        if (fcfg.has("filament_vendor")) ctx.vendor   = fcfg.opt_string("filament_vendor", 0);
        const DynamicPrintConfig& pcfg = pb->printers.get_edited_preset().config;
        if (pcfg.has("nozzle_diameter")) ctx.nozzle = pcfg.opt_float("nozzle_diameter", 0);
    }
    return ctx;
}

ForgeCalibrationRecord forge_capture_current_calibration(const ForgeCaliContext& ctx)
{
    ForgeCalibrationRecord rec;
    rec.printer_id = ctx.printer;
    rec.material   = ctx.material;
    rec.vendor     = ctx.vendor;
    rec.nozzle_mm  = ctx.nozzle;
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
    return rec;
}

bool forge_apply_calibration(const ForgeCalibrationRecord& rec)
{
    Tab* tab = wxGetApp().get_tab(Preset::TYPE_FILAMENT);
    if (tab == nullptr || tab->get_config() == nullptr)
        return false;
    DynamicPrintConfig new_conf = *tab->get_config();
    if (rec.has_flow()) {
        std::vector<double> v{ rec.flow_ratio };
        new_conf.set_key_value("filament_flow_ratio", new ConfigOptionFloatsNullable{ v });
    }
    if (rec.has_pa())
        new_conf.set_key_value("pressure_advance", new ConfigOptionFloats{ rec.pressure_advance });
    if (rec.has_mvs())
        new_conf.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{ rec.max_volumetric_speed });
    tab->load_config(new_conf);
    tab->update_dirty();
    return true;
}

}} // namespace Slic3r::GUI

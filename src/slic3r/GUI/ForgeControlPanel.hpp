#pragma once

#include <wx/panel.h>
#include <functional>
#include <string>

#include "../Utils/ForgeCloudAgent.hpp"   // ForgeLiveState
#include "wxExtensions.hpp"               // ScalableBitmap

class TempInput;
class AxisCtrlButton;
class Button;
class ImageSwitchButton;
class FanSwitchButton;
class StaticBox;
class SwitchBoard;

namespace Slic3r { namespace GUI {

// Reuses OrcaSlicer's native Bambu Device "Control" widgets (TempInput,
// AxisCtrlButton, FanSwitchButton, ImageSwitchButton, Button) to give an
// identical-looking control surface that works for ALL printer brands —
// driven by 3DPrintForge dashboard data/commands instead of a Bambu
// MachineObject. The widget classes themselves have no MachineObject
// dependency (they communicate via wx events + plain setters), so we wire
// their events to dashboard control calls and feed them from ForgeLiveState.
class ForgeControlPanel : public wxPanel
{
public:
    // Command callbacks — supplied by the owner (ForgeFleetPanel), which
    // routes them to ForgeCloudAgent for the currently selected printer.
    struct Callbacks {
        std::function<void(const std::string& axis, double dist_mm)> move;
        std::function<void()>                                        home;
        std::function<void(double amount_mm)>                        extrude;
        std::function<void(const std::string& heater, int temp, int tool)> set_temp;
        std::function<void(int percent)>                             fan;
        std::function<void(bool on)>                                 light;
        std::function<void(int percent)>                             speed;
        std::function<void(int tool)>                                select_tool;
    };

    ForgeControlPanel(wxWindow* parent, Callbacks cb);

    // Push live telemetry into the widgets (temps, active tool, tool count).
    void update_state(const ForgeLiveState& ls);

    // Show/hide the per-tool selector row (multi-extruder printers only).
    void set_tool_count(int n);

private:
    void build();
    void on_axis(wxCommandEvent& evt);          // AxisCtrlButton sector click
    void on_temp_finish(wxCommandEvent& evt);   // TempInput edit committed
    void show_fan_menu();
    void show_speed_menu();

    Callbacks m_cb;
    int       m_active_tool = -1;
    int       m_tool_count  = 0;

    ScalableBitmap m_bmp_axis_home;
    ScalableBitmap m_bmp_lamp_on, m_bmp_lamp_off;
    ScalableBitmap m_bmp_fan_on,  m_bmp_fan_off;
    ScalableBitmap m_bmp_speed,   m_bmp_speed_active;

    TempInput*         m_temp_nozzle  = nullptr;
    TempInput*         m_temp_bed     = nullptr;
    TempInput*         m_temp_chamber = nullptr;
    AxisCtrlButton*    m_axis         = nullptr;
    ImageSwitchButton* m_lamp         = nullptr;
    ImageSwitchButton* m_speed        = nullptr;
    FanSwitchButton*   m_fan          = nullptr;
    StaticBox*         m_fan_panel    = nullptr;
    SwitchBoard*       m_nozzle_switch= nullptr;   // Left/Right toggle (dual nozzle)
    wxBoxSizer*        m_tool_sizer   = nullptr;
    Button*            m_tool_btn[8]  = { nullptr };
};

}} // namespace Slic3r::GUI

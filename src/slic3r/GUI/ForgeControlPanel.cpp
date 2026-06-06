#include "ForgeControlPanel.hpp"
#include "I18N.hpp"

#include "Widgets/TempInput.hpp"
#include "Widgets/AxisCtrlButton.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ImageSwitchButton.hpp"
#include "Widgets/StaticBox.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/Label.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/menu.h>

namespace Slic3r { namespace GUI {

// Palette lifted verbatim from StatusPanel.cpp so the panel matches the
// native Bambu Device "Control" look 1:1 (brand teal swapped in for hover).
namespace {
const wxColour COL_LINE   (238, 238, 238);
const wxColour COL_N1     (238, 238, 238);
const wxColour COL_N2     (206, 206, 206);
const wxColour COL_PRESS  (172, 172, 172);
const wxColour COL_HOVER  (0x00, 0x97, 0x89);   // 3DPrintForge teal
const wxColour COL_DISC   (171, 172, 172);
const wxColour COL_TEXT   (48,  58,  60);
const wxColour COL_FANTEXT(107, 107, 107);
} // namespace

ForgeControlPanel::ForgeControlPanel(wxWindow* parent, Callbacks cb)
    : wxPanel(parent, wxID_ANY)
    , m_cb(std::move(cb))
{
    build();
}

void ForgeControlPanel::build()
{
    m_bmp_axis_home    = ScalableBitmap(this, "monitor_axis_home", 32);
    m_bmp_lamp_on      = ScalableBitmap(this, "monitor_lamp_on", 24);
    m_bmp_lamp_off     = ScalableBitmap(this, "monitor_lamp_off", 24);
    m_bmp_fan_on       = ScalableBitmap(this, "monitor_fan_on", 22);
    m_bmp_fan_off      = ScalableBitmap(this, "monitor_fan_off", 22);
    m_bmp_speed        = ScalableBitmap(this, "monitor_speed", 24);
    m_bmp_speed_active = ScalableBitmap(this, "monitor_speed_active", 24);

    StateColor text_sc(std::make_pair(COL_DISC, (int) StateColor::Disabled),
                       std::make_pair(COL_TEXT, (int) StateColor::Normal));

    auto* root = new wxBoxSizer(wxVERTICAL);

    // ---- temperature + axis + extruder group (rounded StaticBox) ----
    auto* box = new StaticBox(this);
    box->SetBackgroundColor(StateColor(std::make_pair(*wxWHITE, (int) StateColor::Normal)));
    box->SetBorderColor(StateColor(std::make_pair(COL_LINE, (int) StateColor::Normal)));
    box->SetCornerRadius(5);

    auto* content = new wxBoxSizer(wxHORIZONTAL);

    // -- temperature column (nozzle / bed / chamber) --
    StateColor ti_border(std::make_pair(*wxWHITE,  (int) StateColor::Disabled),
                         std::make_pair(COL_HOVER, (int) StateColor::Focused),
                         std::make_pair(COL_HOVER, (int) StateColor::Hovered),
                         std::make_pair(*wxWHITE,  (int) StateColor::Normal));

    auto make_temp = [&](const wxString& normal_icon, const wxString& active_icon,
                         int min_t, int max_t, bool read_only, const wxSize& sz) {
        auto* ti = new TempInput(box, wxWindow::NewControlId(), wxString("_"),
                                 TempInputType::TEMP_OF_NORMAL_TYPE, wxString("_"),
                                 normal_icon, active_icon,
                                 wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
        ti->SetMinSize(sz);
        ti->AddTemp(0);
        ti->SetMinTemp(min_t);
        ti->SetMaxTemp(max_t);
        ti->SetBorderWidth(FromDIP(2));
        ti->SetTextColor(text_sc);
        ti->SetBorderColor(ti_border);
        if (read_only) ti->SetReadOnly(true);
        return ti;
    };

    auto* temps = new wxBoxSizer(wxVERTICAL);
    m_temp_nozzle  = make_temp("monitor_nozzle_temp", "monitor_nozzle_temp_active",
                               0, 350, false, wxSize(FromDIP(145), FromDIP(48)));
    m_temp_bed     = make_temp("monitor_bed_temp", "monitor_bed_temp_active",
                               0, 120, false, wxSize(FromDIP(125), FromDIP(52)));
    m_temp_chamber = make_temp("monitor_frame_temp", "monitor_frame_temp_active",
                               0, 60, true, wxSize(FromDIP(125), FromDIP(52)));
    temps->Add(m_temp_nozzle,  0, wxEXPAND | wxALL, 1);
    temps->Add(m_temp_bed,     0, wxEXPAND | wxALL, 1);
    temps->Add(m_temp_chamber, 0, wxEXPAND | wxALL, 1);
    content->Add(temps, 0, wxEXPAND | wxALL, FromDIP(5));

    auto make_divider = [&]() {
        auto* line = new wxPanel(box);
        line->SetMinSize(wxSize(FromDIP(1), -1));
        line->SetMaxSize(wxSize(FromDIP(1), -1));
        line->SetBackgroundColour(COL_LINE);
        return line;
    };
    content->Add(make_divider(), 0, wxEXPAND);

    // -- axis dial + bed Z control row --
    auto* axisbed = new wxBoxSizer(wxVERTICAL);
    m_axis = new AxisCtrlButton(box, m_bmp_axis_home);
    m_axis->SetTextColor(text_sc);
    m_axis->SetMinSize(wxSize(FromDIP(258), FromDIP(258)));
    m_axis->SetSize(wxSize(FromDIP(258), FromDIP(258)));
    m_axis->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &ForgeControlPanel::on_axis, this);
    axisbed->Add(m_axis, 0, wxALIGN_CENTER);

    StateColor z10_bg(std::make_pair(COL_PRESS, (int) StateColor::Pressed), std::make_pair(COL_N1, (int) StateColor::Normal));
    StateColor z10_bd(std::make_pair(COL_HOVER, (int) StateColor::Hovered), std::make_pair(COL_N1, (int) StateColor::Normal));
    StateColor z1_bg (std::make_pair(COL_PRESS, (int) StateColor::Pressed), std::make_pair(COL_N2, (int) StateColor::Normal));
    StateColor z1_bd (std::make_pair(COL_HOVER, (int) StateColor::Hovered), std::make_pair(COL_N2, (int) StateColor::Normal));

    auto make_z = [&](const wxString& lbl, const wxString& icon, StateColor& bg, StateColor& bd, double dist) {
        auto* b = new Button(box, lbl, icon, 0, 15);
        b->SetFont(::Label::Body_12);
        b->SetBorderWidth(0);
        b->SetBackgroundColor(bg);
        b->SetBorderColor(bd);
        b->SetTextColor(text_sc);
        b->SetMinSize(wxSize(FromDIP(44), FromDIP(40)));
        b->SetSize(wxSize(FromDIP(44), FromDIP(40)));
        b->SetCornerRadius(0);
        b->Bind(wxEVT_BUTTON, [this, dist](wxCommandEvent&) { if (m_cb.move) m_cb.move("Z", dist); });
        return b;
    };
    auto* zrow = new wxBoxSizer(wxHORIZONTAL);
    zrow->Add(make_z("10", "monitor_bed_up",   z10_bg, z10_bd,  10), 0);
    zrow->Add(make_z(" 1", "monitor_bed_up",   z1_bg,  z1_bd,    1), 0, wxLEFT, FromDIP(2));
    zrow->Add(new wxStaticText(box, wxID_ANY, _L("Bed")), 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(6));
    zrow->Add(make_z(" 1", "monitor_bed_down", z1_bg,  z1_bd,   -1), 0);
    zrow->Add(make_z("10", "monitor_bed_down", z10_bg, z10_bd, -10), 0, wxLEFT, FromDIP(2));
    axisbed->Add(zrow, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, FromDIP(6));
    content->Add(axisbed, 1, wxALIGN_CENTER);

    content->Add(make_divider(), 0, wxEXPAND);

    // -- extruder column: tool selector (multi-tool) + extrude / retract --
    auto* extr = new wxBoxSizer(wxVERTICAL);
    m_tool_sizer = new wxBoxSizer(wxHORIZONTAL);
    for (int t = 0; t < 8; ++t) {
        auto* b = new Button(box, wxString::Format("T%d", t + 1));
        b->SetFont(::Label::Body_12);
        b->SetMinSize(wxSize(FromDIP(34), FromDIP(28)));
        b->SetCornerRadius(FromDIP(4));
        b->Bind(wxEVT_BUTTON, [this, t](wxCommandEvent&) { if (m_cb.select_tool) m_cb.select_tool(t); });
        b->Hide();
        m_tool_btn[t] = b;
        m_tool_sizer->Add(b, 0, wxRIGHT | wxBOTTOM, FromDIP(2));
    }
    extr->Add(m_tool_sizer, 0, wxALIGN_CENTER);

    StateColor e_bg(std::make_pair(COL_PRESS, (int) StateColor::Pressed), std::make_pair(COL_N1, (int) StateColor::Normal));
    StateColor e_bd(std::make_pair(COL_HOVER, (int) StateColor::Hovered), std::make_pair(COL_N1, (int) StateColor::Normal));
    auto make_e = [&](const wxString& icon, double amt) {
        auto* b = new Button(box, "", icon, 0, 22);
        b->SetBorderWidth(2);
        b->SetBackgroundColor(e_bg);
        b->SetBorderColor(e_bd);
        b->SetMinSize(wxSize(FromDIP(40), FromDIP(40)));
        b->SetSize(wxSize(FromDIP(40), FromDIP(40)));
        b->SetCornerRadius(FromDIP(12));
        b->Bind(wxEVT_BUTTON, [this, amt](wxCommandEvent&) { if (m_cb.extrude) m_cb.extrude(amt); });
        return b;
    };
    extr->Add(make_e("monitor_extruder_up", 10), 0, wxALIGN_CENTER | wxTOP, FromDIP(6));
    extr->Add(new wxStaticText(box, wxID_ANY, _L("Extruder")), 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, FromDIP(4));
    extr->Add(make_e("monitor_extruder_down", -10), 0, wxALIGN_CENTER);
    content->Add(extr, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(12));

    box->SetSizer(content);
    root->Add(box, 0, wxEXPAND);

    // TempInput posts wxCUSTOMEVT_SET_TEMP_FINISH to its parent (the box).
    box->Bind(wxCUSTOMEVT_SET_TEMP_FINISH, &ForgeControlPanel::on_temp_finish, this);

    // ---- misc control row: speed | lamp | fan ----
    auto* misc = new wxBoxSizer(wxHORIZONTAL);

    m_speed = new ImageSwitchButton(this, m_bmp_speed_active, m_bmp_speed);
    m_speed->SetLabels("100%", "100%");
    m_speed->SetMinSize(wxSize(FromDIP(66), FromDIP(51)));
    m_speed->SetMaxSize(wxSize(FromDIP(66), FromDIP(51)));
    m_speed->SetPadding(FromDIP(3));
    m_speed->SetBorderWidth(FromDIP(2));
    m_speed->SetFont(::Label::Head_13);
    m_speed->SetTextColor(text_sc);
    m_speed->SetValue(false);
    m_speed->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { show_speed_menu(); });
    misc->Add(m_speed, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(8));

    m_lamp = new ImageSwitchButton(this, m_bmp_lamp_on, m_bmp_lamp_off);
    m_lamp->SetLabels(_L("Lamp"), _L("Lamp"));
    m_lamp->SetMinSize(wxSize(FromDIP(66), FromDIP(51)));
    m_lamp->SetMaxSize(wxSize(FromDIP(66), FromDIP(51)));
    m_lamp->SetPadding(FromDIP(3));
    m_lamp->SetBorderWidth(FromDIP(2));
    m_lamp->SetFont(::Label::Head_13);
    m_lamp->SetTextColor(text_sc);
    m_lamp->SetValue(false);
    m_lamp->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent&) {
        bool nv = !m_lamp->GetValue();
        m_lamp->SetValue(nv);
        if (m_cb.light) m_cb.light(nv);
    });
    misc->Add(m_lamp, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(8));

    m_fan_panel = new StaticBox(this);
    m_fan_panel->SetMinSize(wxSize(FromDIP(136), FromDIP(55)));
    m_fan_panel->SetMaxSize(wxSize(FromDIP(136), FromDIP(55)));
    m_fan_panel->SetBackgroundColor(*wxWHITE);
    m_fan_panel->SetBorderWidth(0);
    m_fan_panel->SetCornerRadius(0);
    auto* fan_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_fan = new FanSwitchButton(m_fan_panel, m_bmp_fan_on, m_bmp_fan_off);
    m_fan->SetValue(false);
    m_fan->SetMinSize(wxSize(FromDIP(132), FromDIP(51)));
    m_fan->SetMaxSize(wxSize(FromDIP(132), FromDIP(51)));
    m_fan->SetPadding(FromDIP(1));
    m_fan->SetBorderWidth(0);
    m_fan->SetCornerRadius(0);
    m_fan->SetFont(::Label::Body_10);
    m_fan->UseTextFan();
    m_fan->SetTextColor(StateColor(std::make_pair(COL_DISC, (int) StateColor::Disabled),
                                   std::make_pair(COL_FANTEXT, (int) StateColor::Normal)));
    m_fan->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent&) { show_fan_menu(); });
    fan_sizer->Add(m_fan, 1, wxALIGN_CENTER);
    m_fan_panel->SetSizer(fan_sizer);
    misc->Add(m_fan_panel, 0, wxALIGN_CENTER);

    root->Add(misc, 0, wxALIGN_CENTER | wxTOP, FromDIP(8));

    SetSizer(root);
}

void ForgeControlPanel::on_axis(wxCommandEvent& evt)
{
    switch (evt.GetInt()) {     // AxisCtrlButton::CurrentPos
        case 0: if (m_cb.move) m_cb.move("Y",  10); break;  // OUTER_UP
        case 1: if (m_cb.move) m_cb.move("X", -10); break;  // OUTER_LEFT
        case 2: if (m_cb.move) m_cb.move("Y", -10); break;  // OUTER_DOWN
        case 3: if (m_cb.move) m_cb.move("X",  10); break;  // OUTER_RIGHT
        case 4: if (m_cb.move) m_cb.move("Y",   1); break;  // INNER_UP
        case 5: if (m_cb.move) m_cb.move("X",  -1); break;  // INNER_LEFT
        case 6: if (m_cb.move) m_cb.move("Y",  -1); break;  // INNER_DOWN
        case 7: if (m_cb.move) m_cb.move("X",   1); break;  // INNER_RIGHT
        case 8: if (m_cb.home) m_cb.home();         break;  // INNER_HOME
        default: break;
    }
}

void ForgeControlPanel::on_temp_finish(wxCommandEvent& evt)
{
    const int id = evt.GetInt();
    auto read = [](TempInput* ti) -> int {
        long v = 0;
        if (ti && ti->GetTextCtrl()) ti->GetTextCtrl()->GetValue().ToLong(&v);
        return static_cast<int>(v);
    };
    if (m_temp_bed && id == m_temp_bed->GetType()) {
        if (m_cb.set_temp) m_cb.set_temp("bed", read(m_temp_bed), -1);
    } else if (m_temp_nozzle && id == m_temp_nozzle->GetType()) {
        if (m_cb.set_temp) m_cb.set_temp("nozzle", read(m_temp_nozzle),
                                         m_tool_count > 1 ? m_active_tool : -1);
    }
}

void ForgeControlPanel::show_fan_menu()
{
    wxMenu menu;
    for (int p : { 0, 25, 50, 75, 100 }) {
        wxString lbl = (p == 0) ? _L("Off") : wxString::Format("%d%%", p);
        int item_id = menu.Append(wxID_ANY, lbl)->GetId();
        menu.Bind(wxEVT_MENU, [this, p](wxCommandEvent&) {
            if (m_cb.fan) m_cb.fan(p);
            if (m_fan) { m_fan->setFanValue(p); m_fan->SetValue(p > 0); }
        }, item_id);
    }
    PopupMenu(&menu);
}

void ForgeControlPanel::show_speed_menu()
{
    struct Sp { const char* lbl; int pct; };
    wxMenu menu;
    for (const Sp& s : { Sp{"Silent  50%", 50}, Sp{"Normal  100%", 100},
                         Sp{"Sport  125%", 125}, Sp{"Ludicrous  166%", 166} }) {
        int pct = s.pct;
        int item_id = menu.Append(wxID_ANY, wxString::FromUTF8(s.lbl))->GetId();
        menu.Bind(wxEVT_MENU, [this, pct](wxCommandEvent&) {
            if (m_cb.speed) m_cb.speed(pct);
            if (m_speed) {
                wxString s = wxString::Format("%d%%", pct);
                m_speed->SetLabels(s, s);
                m_speed->SetValue(pct != 100);
            }
        }, item_id);
    }
    PopupMenu(&menu);
}

void ForgeControlPanel::set_tool_count(int n)
{
    if (n == m_tool_count) return;
    m_tool_count = n;
    const bool multi = n > 1;
    for (int t = 0; t < 8; ++t)
        if (m_tool_btn[t]) m_tool_btn[t]->Show(multi && t < n);
    Layout();
}

void ForgeControlPanel::update_state(const ForgeLiveState& ls)
{
    set_tool_count(static_cast<int>(ls.tools.size()));
    if (ls.active_tool >= 0) m_active_tool = ls.active_tool;

    // Nozzle: show the active tool's temp/target when multi-tool, else the
    // single nozzle reading.
    double cur = ls.nozzle_temp, tag = -1;
    if (!ls.tools.empty()) {
        int a = (m_active_tool >= 0 && m_active_tool < static_cast<int>(ls.tools.size()))
                ? m_active_tool : 0;
        cur = ls.tools[a].temp;
        tag = ls.tools[a].target;
    }
    if (m_temp_nozzle) {
        if (cur >= 0) m_temp_nozzle->SetCurrTemp(static_cast<int>(cur));
        if (tag >= 0) m_temp_nozzle->SetTagTemp(static_cast<int>(tag));
        if (tag > 0 && tag - cur >= 2) m_temp_nozzle->SetIconActive();
        else                           m_temp_nozzle->SetIconNormal();
    }
    if (m_temp_bed && ls.bed_temp >= 0)         m_temp_bed->SetCurrTemp(static_cast<int>(ls.bed_temp));
    if (m_temp_chamber && ls.chamber_temp >= 0) m_temp_chamber->SetCurrTemp(static_cast<int>(ls.chamber_temp));
}

}} // namespace Slic3r::GUI

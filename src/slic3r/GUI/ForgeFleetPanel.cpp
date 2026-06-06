#include "ForgeFleetPanel.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"

#include "libslic3r/AppConfig.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/statbmp.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/gbsizer.h>
#include <wx/graphics.h>
#include <wx/dcbuffer.h>

#include <cmath>
#include <functional>

namespace Slic3r { namespace GUI {

namespace {
constexpr int POLL_INTERVAL_MS = 5000;
constexpr int COL_NAME     = 0;
constexpr int COL_VENDOR   = 1;
constexpr int COL_HOST     = 2;
constexpr int COL_STATUS   = 3;
constexpr int COL_PROGRESS = 4;

constexpr double kPi = 3.14159265358979323846;

// Brand accent (3DPrintForge teal) and dial palette.
const wxColour kAccent (0x00, 0x97, 0x89);
const wxColour kAccentDk(0x00, 0x66, 0x5b);
const wxColour kDialBase(0x3a, 0x3f, 0x44);
const wxColour kDialEdge(0x55, 0x5b, 0x61);
const wxColour kDialText(0xe6, 0xe6, 0xe6);

// Owner-drawn circular X/Y jog dial — mirrors the Bambu Device "Control"
// dial: Y at top, -Y bottom, X right, -X left, Home in the centre. Clicking
// a sector jogs that axis; the centre homes. Hovering highlights the sector.
class ForgeJogDial : public wxPanel
{
public:
    ForgeJogDial(wxWindow* parent,
                 std::function<void(const std::string&, double)> move_cb,
                 std::function<void()> home_cb,
                 double step_mm = 10.0)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(168, 168))
        , m_move(std::move(move_cb)), m_home(std::move(home_cb)), m_step(step_mm)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT,        &ForgeJogDial::on_paint,  this);
        Bind(wxEVT_LEFT_DOWN,    &ForgeJogDial::on_click,  this);
        Bind(wxEVT_MOTION,       &ForgeJogDial::on_motion, this);
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { if (m_hover != N) { m_hover = N; Refresh(); } });
        SetCursor(wxCursor(wxCURSOR_HAND));
    }

private:
    enum Sector { N = -1, UP, RIGHT, DOWN, LEFT, HOME };
    std::function<void(const std::string&, double)> m_move;
    std::function<void()> m_home;
    double m_step;
    int    m_hover = N;

    void geom(double& cx, double& cy, double& outer, double& inner) const
    {
        wxSize sz = GetClientSize();
        cx = sz.x / 2.0; cy = sz.y / 2.0;
        outer = std::min(cx, cy) - 3.0;
        inner = outer * 0.36;
    }

    int hit(const wxPoint& p) const
    {
        double cx, cy, outer, inner; geom(cx, cy, outer, inner);
        double dx = p.x - cx, dy = p.y - cy;
        double r = std::sqrt(dx * dx + dy * dy);
        if (r <= inner) return HOME;
        if (r > outer)  return N;
        double deg = std::atan2(dy, dx) * 180.0 / kPi; // 0 = right, +down
        if (deg >= -45 && deg < 45)   return RIGHT;
        if (deg >= 45 && deg < 135)   return DOWN;
        if (deg >= -135 && deg < -45) return UP;
        return LEFT;
    }

    void on_motion(wxMouseEvent& e) { int h = hit(e.GetPosition()); if (h != m_hover) { m_hover = h; Refresh(); } }

    void on_click(wxMouseEvent& e)
    {
        switch (hit(e.GetPosition())) {
            case UP:    if (m_move) m_move("Y",  m_step); break;
            case DOWN:  if (m_move) m_move("Y", -m_step); break;
            case RIGHT: if (m_move) m_move("X",  m_step); break;
            case LEFT:  if (m_move) m_move("X", -m_step); break;
            case HOME:  if (m_home) m_home();             break;
            default: break;
        }
    }

    void wedge(wxGraphicsContext* gc, double cx, double cy, double outer,
               double a0, double a1, const wxColour& fill) const
    {
        wxGraphicsPath path = gc->CreatePath();
        path.MoveToPoint(cx, cy);
        path.AddArc(cx, cy, outer, a0, a1, true);
        path.CloseSubpath();
        gc->SetBrush(wxBrush(fill));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->FillPath(path);
    }

    void on_paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
        if (!gc) return;

        double cx, cy, outer, inner; geom(cx, cy, outer, inner);

        // Base disc.
        gc->SetBrush(wxBrush(kDialBase));
        gc->SetPen(wxPen(kDialEdge, 1));
        gc->DrawEllipse(cx - outer, cy - outer, outer * 2, outer * 2);

        // Hover highlight on the active sector.
        const double q = kPi / 4.0;
        if (m_hover == RIGHT) wedge(gc, cx, cy, outer, -q, q, kAccentDk);
        else if (m_hover == DOWN)  wedge(gc, cx, cy, outer, q, 3 * q, kAccentDk);
        else if (m_hover == LEFT)  wedge(gc, cx, cy, outer, 3 * q, 5 * q, kAccentDk);
        else if (m_hover == UP)    wedge(gc, cx, cy, outer, -3 * q, -q, kAccentDk);

        // Divider lines between sectors.
        gc->SetPen(wxPen(kDialEdge, 1));
        for (double a = q; a < 2 * kPi; a += 2 * q) {
            wxGraphicsPath ln = gc->CreatePath();
            ln.MoveToPoint(cx + inner * std::cos(a), cy + inner * std::sin(a));
            ln.AddLineToPoint(cx + outer * std::cos(a), cy + outer * std::sin(a));
            gc->StrokePath(ln);
        }

        // Centre Home button.
        gc->SetBrush(wxBrush(m_hover == HOME ? kAccent : kDialEdge));
        gc->SetPen(wxPen(kDialEdge, 1));
        gc->DrawEllipse(cx - inner, cy - inner, inner * 2, inner * 2);

        // Labels.
        gc->SetFont(GetFont(), kDialText);
        double mid = (inner + outer) / 2.0;
        auto label = [&](const wxString& s, double x, double y) {
            double tw, th, dd, ee; gc->GetTextExtent(s, &tw, &th, &dd, &ee);
            gc->DrawText(s, x - tw / 2, y - th / 2);
        };
        label("Y",  cx,        cy - mid);
        label("-Y", cx,        cy + mid);
        label("X",  cx + mid,  cy);
        label("-X", cx - mid,  cy);
        gc->SetFont(GetFont(), m_hover == HOME ? *wxWHITE : kDialText);
        label(_L("Home"), cx, cy);

        delete gc;
    }
};
} // namespace

ForgeFleetPanel::ForgeFleetPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
    , m_agent(std::make_unique<ForgeCloudAgent>())
    , m_poll_timer(this)
{
    SetBackgroundColour(*wxWHITE);

    // Restore saved server URL from AppConfig if present.
    if (auto* cfg = wxGetApp().app_config) {
        std::string saved = cfg->get("forge_server_url");
        if (!saved.empty()) m_agent->set_server_url(saved);
    }

    build_ui();

    Bind(wxEVT_TIMER, &ForgeFleetPanel::on_timer, this);
    Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
        if (e.IsShown()) on_show();
        else             on_hide();
        e.Skip();
    });
}

ForgeFleetPanel::~ForgeFleetPanel()
{
    if (m_poll_timer.IsRunning()) m_poll_timer.Stop();
}

void ForgeFleetPanel::build_ui()
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, _L("3DPrintForge Devices"));
    auto f = title->GetFont(); f.SetPointSize(f.GetPointSize() + 4); f.MakeBold();
    title->SetFont(f);
    root->Add(title, 0, wxALL, 14);

    m_server_label = new wxStaticText(this, wxID_ANY,
        wxString::Format(_L("Server: %s"), wxString::FromUTF8(m_agent->server_url())));
    root->Add(m_server_label, 0, wxLEFT | wxRIGHT, 14);

    m_status_label = new wxStaticText(this, wxID_ANY, _L("Not signed in. Click Login."));
    root->Add(m_status_label, 0, wxLEFT | wxRIGHT | wxBOTTOM, 14);

    // Action buttons.
    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    m_btn_login     = new wxButton(this, wxID_ANY, _L("Login..."));
    m_btn_configure = new wxButton(this, wxID_ANY, _L("Server URL..."));
    m_btn_refresh   = new wxButton(this, wxID_ANY, _L("Refresh"));
    m_btn_print     = new wxButton(this, wxID_ANY, _L("Send active gcode to selected"));
    btn_row->Add(m_btn_login,     0, wxRIGHT, 6);
    btn_row->Add(m_btn_configure, 0, wxRIGHT, 6);
    btn_row->Add(m_btn_refresh,   0, wxRIGHT, 12);
    btn_row->AddStretchSpacer(1);
    btn_row->Add(m_btn_print,     0);
    root->Add(btn_row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 14);

    m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL);
    m_list->AppendColumn(_L("Printer"),  wxLIST_FORMAT_LEFT, 200);
    m_list->AppendColumn(_L("Vendor"),   wxLIST_FORMAT_LEFT, 120);
    m_list->AppendColumn(_L("Host"),     wxLIST_FORMAT_LEFT, 160);
    m_list->AppendColumn(_L("Status"),   wxLIST_FORMAT_LEFT, 140);
    m_list->AppendColumn(_L("Progress"), wxLIST_FORMAT_LEFT, 100);

    // Layout mirrors the Bambu Device monitor: a narrow printer list on the
    // left, a large camera (+ status / progress / filament) in the centre, and
    // the tall Control panel on the right.
    m_list->SetMinSize(wxSize(250, 470));
    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(m_list, 0, wxEXPAND | wxRIGHT, 12);

    auto* detail = new wxBoxSizer(wxVERTICAL);
    m_camera = new wxStaticBitmap(this, wxID_ANY, wxBitmap());
    m_camera->SetMinSize(wxSize(460, 345));
    detail->Add(m_camera, 1, wxEXPAND | wxBOTTOM, 8);
    m_detail_label = new wxStaticText(this, wxID_ANY, _L("Select a printer to see details."));
    detail->Add(m_detail_label, 0, wxBOTTOM, 8);

    // Filament slots — a color swatch + material per toolhead, mirroring the
    // Snapmaker Orca device layout. Clicking a slot selects (picks) that tool.
    // Populated/shown for multi-tool printers in update_detail().
    m_filament_row = new wxPanel(this, wxID_ANY);
    {
        auto* fr = new wxBoxSizer(wxHORIZONTAL);
        fr->Add(new wxStaticText(m_filament_row, wxID_ANY, _L("Filament:")),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        for (int i = 0; i < 4; ++i) {
            auto* slot = new wxBoxSizer(wxVERTICAL);
            m_slot[i] = new wxPanel(m_filament_row, wxID_ANY, wxDefaultPosition, wxSize(46, 22),
                                    wxBORDER_SIMPLE);
            m_slot[i]->SetBackgroundColour(wxColour(90, 90, 90));
            m_slot[i]->SetCursor(wxCursor(wxCURSOR_HAND));
            m_slot_lbl[i] = new wxStaticText(m_filament_row, wxID_ANY, wxString::Format("T%d", i + 1),
                                             wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
            // Clicking a swatch or its label picks (tool-changes to) that extruder.
            auto pick = [this, i](wxMouseEvent&) {
                if (!m_selected_printer_id.empty()) {
                    m_agent->control_tool(m_selected_printer_id, i);
                    update_detail();
                }
            };
            m_slot[i]->Bind(wxEVT_LEFT_DOWN, pick);
            m_slot_lbl[i]->Bind(wxEVT_LEFT_DOWN, pick);
            slot->Add(m_slot[i], 0, wxALIGN_CENTER | wxBOTTOM, 2);
            slot->Add(m_slot_lbl[i], 0, wxALIGN_CENTER);
            fr->Add(slot, 0, wxRIGHT, 8);
        }
        m_filament_row->SetSizer(fr);
    }
    m_filament_row->Hide();
    detail->Add(m_filament_row, 0, wxTOP, 8);

    // Print controls for the selected printer (routed through the dashboard,
    // which dispatches per brand — Bambu MQTT, Moonraker REST, etc.).
    auto* ctrl_row = new wxBoxSizer(wxHORIZONTAL);
    m_btn_pause  = new wxButton(this, wxID_ANY, _L("Pause"));
    m_btn_resume = new wxButton(this, wxID_ANY, _L("Resume"));
    m_btn_stop   = new wxButton(this, wxID_ANY, _L("Stop"));
    ctrl_row->Add(m_btn_pause,  0, wxRIGHT, 6);
    ctrl_row->Add(m_btn_resume, 0, wxRIGHT, 6);
    ctrl_row->Add(m_btn_stop,   0);
    detail->Add(ctrl_row, 0, wxTOP, 8);

    // Control panel — shown only for Klipper/Moonraker printers (jog/extrude/
    // home/tool + fan/lamp/speed route to gcode via the dashboard). Laid out
    // to mirror the Bambu Device "Control" tab: a tool/jog/extruder column on
    // the left, fan/lamp/speed on the right.
    m_motion_panel = new wxPanel(this, wxID_ANY);
    {
        auto* outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(new wxStaticText(m_motion_panel, wxID_ANY, _L("Control")), 0, wxBOTTOM, 6);

        auto* cols = new wxBoxSizer(wxHORIZONTAL);

        // ---- Left column: tools, jog cross + Z (Bed), extruder ----
        auto* left = new wxBoxSizer(wxVERTICAL);

        auto* tools = new wxBoxSizer(wxHORIZONTAL);
        tools->Add(new wxStaticText(m_motion_panel, wxID_ANY, _L("Tools:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        for (int t = 0; t < 4; ++t) {
            auto* b = new wxButton(m_motion_panel, wxID_ANY, wxString::Format("T%d", t + 1), wxDefaultPosition, wxSize(40, -1));
            b->Bind(wxEVT_BUTTON, [this, t](wxCommandEvent&) {
                if (!m_selected_printer_id.empty()) m_agent->control_tool(m_selected_printer_id, t);
            });
            tools->Add(b, 0, wxRIGHT, 4);
        }
        left->Add(tools, 0, wxBOTTOM, 8);

        // Circular X/Y jog dial (Home centre) beside a vertical Z (Bed) column,
        // exactly like the Bambu Device "Control" dial.
        auto* dialrow = new wxBoxSizer(wxHORIZONTAL);
        auto* dial = new ForgeJogDial(m_motion_panel,
            [this](const std::string& ax, double d) {
                if (!m_selected_printer_id.empty()) m_agent->control_move(m_selected_printer_id, ax, d);
            },
            [this]() {
                if (!m_selected_printer_id.empty()) m_agent->control_home(m_selected_printer_id);
            });
        dialrow->Add(dial, 0, wxRIGHT, 14);

        auto mkz = [&](const char* lbl, double d) {
            auto* b = new wxButton(m_motion_panel, wxID_ANY, lbl, wxDefaultPosition, wxSize(52, 30));
            b->Bind(wxEVT_BUTTON, [this, d](wxCommandEvent&) {
                if (!m_selected_printer_id.empty()) m_agent->control_move(m_selected_printer_id, "Z", d);
            });
            return b;
        };
        auto* zcol = new wxBoxSizer(wxVERTICAL);
        zcol->Add(mkz("Z +10", 10), 0, wxBOTTOM, 3);
        zcol->Add(mkz("Z +1",   1), 0, wxBOTTOM, 6);
        zcol->Add(new wxStaticText(m_motion_panel, wxID_ANY, _L("Bed")), 0, wxALIGN_CENTER | wxBOTTOM, 6);
        zcol->Add(mkz("Z -1",  -1), 0, wxBOTTOM, 3);
        zcol->Add(mkz("Z -10", -10), 0);
        dialrow->Add(zcol, 0, wxALIGN_CENTER_VERTICAL);
        left->Add(dialrow, 0, wxBOTTOM, 8);

        auto* erow = new wxBoxSizer(wxHORIZONTAL);
        auto* ext = new wxButton(m_motion_panel, wxID_ANY, _L("Extrude"));
        auto* ret = new wxButton(m_motion_panel, wxID_ANY, _L("Retract"));
        ext->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (!m_selected_printer_id.empty()) m_agent->control_extrude(m_selected_printer_id, 5);  });
        ret->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (!m_selected_printer_id.empty()) m_agent->control_extrude(m_selected_printer_id, -5); });
        erow->Add(new wxStaticText(m_motion_panel, wxID_ANY, _L("Extruder:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        erow->Add(ext, 0, wxRIGHT, 4);
        erow->Add(ret, 0);
        left->Add(erow, 0);

        cols->Add(left, 0, wxRIGHT, 28);

        // ---- Right column: fan, lamp, speed (as on the reference) ----
        auto* right = new wxBoxSizer(wxVERTICAL);

        auto* fanrow = new wxBoxSizer(wxHORIZONTAL);
        fanrow->Add(new wxStaticText(m_motion_panel, wxID_ANY, _L("Fan")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        for (int pct : { 0, 50, 100 }) {
            wxString lbl = (pct == 0) ? _L("Off") : wxString::Format("%d%%", pct);
            auto* b = new wxButton(m_motion_panel, wxID_ANY, lbl, wxDefaultPosition, wxSize(48, -1));
            b->Bind(wxEVT_BUTTON, [this, pct](wxCommandEvent&) { if (!m_selected_printer_id.empty()) m_agent->control_fan(m_selected_printer_id, pct); });
            fanrow->Add(b, 0, wxRIGHT, 4);
        }
        right->Add(fanrow, 0, wxBOTTOM, 8);

        auto* lamprow = new wxBoxSizer(wxHORIZONTAL);
        lamprow->Add(new wxStaticText(m_motion_panel, wxID_ANY, _L("Lamp")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        auto* lon  = new wxButton(m_motion_panel, wxID_ANY, _L("On"),  wxDefaultPosition, wxSize(48, -1));
        auto* loff = new wxButton(m_motion_panel, wxID_ANY, _L("Off"), wxDefaultPosition, wxSize(48, -1));
        lon ->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (!m_selected_printer_id.empty()) m_agent->control_light(m_selected_printer_id, true);  });
        loff->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (!m_selected_printer_id.empty()) m_agent->control_light(m_selected_printer_id, false); });
        lamprow->Add(lon, 0, wxRIGHT, 4);
        lamprow->Add(loff, 0);
        right->Add(lamprow, 0, wxBOTTOM, 8);

        auto* speedrow = new wxBoxSizer(wxHORIZONTAL);
        speedrow->Add(new wxStaticText(m_motion_panel, wxID_ANY, _L("Speed")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        struct SP { const char* lbl; int pct; };
        for (const SP& s : { SP{"Silent", 50}, SP{"Normal", 100}, SP{"Sport", 125}, SP{"Max", 166} }) {
            auto* b = new wxButton(m_motion_panel, wxID_ANY, s.lbl, wxDefaultPosition, wxSize(60, -1));
            int pct = s.pct;
            b->Bind(wxEVT_BUTTON, [this, pct](wxCommandEvent&) { if (!m_selected_printer_id.empty()) m_agent->control_speed(m_selected_printer_id, pct); });
            speedrow->Add(b, 0, wxRIGHT, 4);
        }
        right->Add(speedrow, 0);

        cols->Add(right, 0);
        outer->Add(cols, 0);
        m_motion_panel->SetSizer(outer);
    }
    m_motion_panel->Hide();

    body->Add(detail, 1, wxEXPAND | wxRIGHT, 12);
    body->Add(m_motion_panel, 0, wxEXPAND);

    root->Add(body, 1, wxALL | wxEXPAND, 14);

    SetSizer(root);

    m_btn_login    ->Bind(wxEVT_BUTTON, &ForgeFleetPanel::on_login, this);
    m_btn_configure->Bind(wxEVT_BUTTON, &ForgeFleetPanel::on_configure, this);
    m_btn_refresh  ->Bind(wxEVT_BUTTON, &ForgeFleetPanel::on_refresh, this);
    m_btn_print    ->Bind(wxEVT_BUTTON, &ForgeFleetPanel::on_print, this);
    m_list->Bind(wxEVT_LIST_ITEM_SELECTED, &ForgeFleetPanel::on_select, this);
    m_btn_pause ->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ send_control("pause"); });
    m_btn_resume->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ send_control("resume"); });
    m_btn_stop  ->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ send_control("stop"); });
}

void ForgeFleetPanel::send_control(const std::string& action)
{
    if (m_selected_printer_id.empty()) {
        wxMessageBox(_L("Select a printer first."), _L("3DPrintForge Devices"),
                     wxOK | wxICON_INFORMATION, this);
        return;
    }
    if (action == "stop" &&
        wxMessageBox(_L("Stop the print on this printer?"), _L("Stop print"),
                     wxYES_NO | wxICON_WARNING, this) != wxYES)
        return;

    bool ok = m_agent->control_printer(m_selected_printer_id, action);
    if (!ok)
        wxMessageBox(wxString::Format(_L("Command failed: %s"),
                     wxString::FromUTF8(m_agent->auth_state().last_error)),
                     _L("3DPrintForge Devices"), wxOK | wxICON_ERROR, this);
    update_detail();
}

void ForgeFleetPanel::on_select(wxListEvent& evt)
{
    long idx = evt.GetIndex();
    m_selected_printer_id = (idx >= 0 && idx < (long)m_printers.size())
                            ? m_printers[idx].id : std::string();
    update_detail();
}

void ForgeFleetPanel::update_detail()
{
    if (m_selected_printer_id.empty() || !m_detail_label) {
        if (m_detail_label) m_detail_label->SetLabel(_L("Select a printer to see details."));
        if (m_camera) m_camera->SetBitmap(wxBitmap());
        return;
    }
    // Find the selected printer in the current roster.
    const ForgePrinter* p = nullptr;
    for (const auto& fp : m_printers)
        if (fp.id == m_selected_printer_id) { p = &fp; break; }
    if (!p) return;

    // Motion/tool controls only apply to gcode (Klipper/Moonraker) printers.
    const bool gcode_capable = (p->vendor == "moonraker" || p->vendor == "klipper");
    if (m_motion_panel && m_motion_panel->IsShown() != gcode_capable) {
        m_motion_panel->Show(gcode_capable);
        Layout();
    }

    wxString info = wxString::FromUTF8(p->name);
    if (!p->vendor.empty()) info += " · " + wxString::FromUTF8(p->vendor);
    wxString st = p->status.empty() ? wxString::FromUTF8(p->state) : wxString::FromUTF8(p->status);
    if (!st.empty()) info += "\n" + _L("Status: ") + st;
    if (!p->current_job.empty()) info += "\n" + _L("Job: ") + wxString::FromUTF8(p->current_job);
    if (p->progress_pct > 0) info += wxString::Format("  (%d%%)", p->progress_pct);

    // Live runtime telemetry (temps/progress) — best-effort, read-only.
    ForgeLiveState ls = m_agent->get_printer_state(m_selected_printer_id);
    if (ls.ok) {
        if (ls.progress_pct >= 0) info += "\n" + _L("Progress: ") + wxString::Format("%d%%", ls.progress_pct);
        if (!ls.tools.empty()) {
            // Per-toolhead temps + loaded filament (e.g. Snapmaker U1).
            for (int i = 0; i < (int)ls.tools.size(); ++i) {
                const ForgeToolState& t = ls.tools[i];
                wxString line = wxString::Format("\nT%d%s: ", i + 1, (i == ls.active_tool) ? " *" : "");
                if (t.temp >= 0)   line += wxString::Format("%.0f", t.temp);
                if (t.target > 0)  line += wxString::Format("/%.0f", t.target);
                line += "°C";
                if (!t.filament.empty()) line += "  " + wxString::FromUTF8(t.filament);
                info += line;
            }
        } else if (ls.nozzle_temp >= 0) {
            info += wxString::Format("\n" + _L("Nozzle %.0f°C"), ls.nozzle_temp);
        }
        wxString temps;
        if (ls.bed_temp >= 0)     temps += wxString::Format(_L("Bed %.0f°C  "), ls.bed_temp);
        if (ls.chamber_temp >= 0) temps += wxString::Format(_L("Chamber %.0f°C"), ls.chamber_temp);
        if (!temps.empty()) info += "\n" + temps;
    }
    m_detail_label->SetLabel(info);

    // Update the filament slots — color swatch + material per toolhead.
    // Shown only for multi-tool printers; the active tool's label is bold.
    if (m_filament_row) {
        const int ntools = (int)ls.tools.size();
        const bool show_slots = ntools > 1;
        for (int i = 0; i < 4; ++i) {
            if (!m_slot[i] || !m_slot_lbl[i]) continue;
            const bool has = i < ntools;
            m_slot[i]->Show(has);
            m_slot_lbl[i]->Show(has);
            if (!has) continue;
            const ForgeToolState& t = ls.tools[i];
            wxColour col(90, 90, 90);
            if (!t.color.empty() && t.color[0] == '#') {
                wxColour parsed(wxString::FromUTF8(t.color));
                if (parsed.IsOk()) col = parsed;
            }
            m_slot[i]->SetBackgroundColour(col);
            m_slot[i]->Refresh();
            wxString mat = t.filament.empty() ? _L("Empty") : wxString::FromUTF8(t.filament);
            m_slot_lbl[i]->SetLabel(wxString::Format("T%d  %s", i + 1, mat));
            wxFont f = m_slot_lbl[i]->GetFont();
            f.SetWeight((i == ls.active_tool) ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
            m_slot_lbl[i]->SetFont(f);
        }
        if (m_filament_row->IsShown() != show_slots)
            m_filament_row->Show(show_slots);
    }

    // Pull a fresh camera frame (JPEG) and show it; gracefully blank if none.
    std::string jpeg = m_agent->get_camera_frame(m_selected_printer_id);
    if (!jpeg.empty()) {
        wxMemoryInputStream mis(jpeg.data(), jpeg.size());
        wxImage img;
        if (img.LoadFile(mis, wxBITMAP_TYPE_JPEG) && img.IsOk()) {
            wxSize sz = m_camera->GetSize();
            if (sz.GetWidth() > 16 && sz.GetHeight() > 16)
                img = img.Scale(sz.GetWidth(), sz.GetHeight(), wxIMAGE_QUALITY_HIGH);
            m_camera->SetBitmap(wxBitmap(img));
        }
    } else {
        m_camera->SetBitmap(wxBitmap());
    }
    Layout();
}

void ForgeFleetPanel::on_show()
{
    refresh_printer_list();
    if (!m_poll_timer.IsRunning())
        m_poll_timer.Start(POLL_INTERVAL_MS);
}

void ForgeFleetPanel::on_hide()
{
    if (m_poll_timer.IsRunning()) m_poll_timer.Stop();
}

void ForgeFleetPanel::on_timer(wxTimerEvent& /*evt*/)
{
    refresh_printer_list();
    if (!m_selected_printer_id.empty())
        update_detail();   // refresh status + camera frame for the open printer
}

void ForgeFleetPanel::on_refresh(wxCommandEvent& /*evt*/)
{
    refresh_printer_list();
}

void ForgeFleetPanel::on_configure(wxCommandEvent& /*evt*/)
{
    wxTextEntryDialog dlg(this, _L("3DPrintForge Server URL"),
                          _L("Server"), wxString::FromUTF8(m_agent->server_url()));
    if (dlg.ShowModal() != wxID_OK) return;
    const std::string url = dlg.GetValue().ToStdString();
    m_agent->set_server_url(url);
    m_server_label->SetLabel(wxString::Format(_L("Server: %s"), wxString::FromUTF8(url)));
    if (auto* cfg = wxGetApp().app_config) {
        cfg->set("forge_server_url", url);
        cfg->save();
    }
    refresh_printer_list();
}

void ForgeFleetPanel::on_login(wxCommandEvent& /*evt*/)
{
    wxTextEntryDialog user_dlg(this, _L("Username"), _L("3DPrintForge login"));
    if (user_dlg.ShowModal() != wxID_OK) return;
    wxPasswordEntryDialog pass_dlg(this, _L("Password"), _L("3DPrintForge login"));
    if (pass_dlg.ShowModal() != wxID_OK) return;

    const std::string u = user_dlg.GetValue().ToStdString();
    const std::string p = pass_dlg.GetValue().ToStdString();

    update_status_bar("Signing in...");
    if (m_agent->login(u, p)) {
        update_status_bar("Signed in as " + m_agent->auth_state().username);
        refresh_printer_list();
    } else {
        update_status_bar("Login failed: " + m_agent->auth_state().last_error);
    }
}

void ForgeFleetPanel::on_print(wxCommandEvent& /*evt*/)
{
    long sel = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= (long)m_printers.size()) {
        wxMessageBox(_L("Pick a printer first."), _L("3DPrintForge Devices"), wxICON_INFORMATION);
        return;
    }
    // Real gcode-send path comes when we wire Plater export → fleet.
    // Stub for now so the button is wired and visible.
    wxMessageBox(wxString::Format(_L("Print queue for %s — not wired yet."),
                                  wxString::FromUTF8(m_printers[sel].name)),
                 _L("3DPrintForge Devices"), wxICON_INFORMATION);
}

void ForgeFleetPanel::refresh_printer_list()
{
    m_printers = m_agent->list_printers();
    m_list->DeleteAllItems();
    for (size_t i = 0; i < m_printers.size(); ++i) {
        const auto& p = m_printers[i];
        long row = m_list->InsertItem((long)i, wxString::FromUTF8(p.name));
        m_list->SetItem(row, COL_VENDOR, wxString::FromUTF8(p.vendor));
        m_list->SetItem(row, COL_HOST,   wxString::FromUTF8(p.ip));
        wxString status = p.status.empty() ? wxString::FromUTF8(p.state) : wxString::FromUTF8(p.status);
        m_list->SetItem(row, COL_STATUS, status);
        if (p.progress_pct > 0)
            m_list->SetItem(row, COL_PROGRESS, wxString::Format("%d%%", p.progress_pct));
    }

    if (m_printers.empty()) {
        const std::string err = m_agent->auth_state().last_error;
        if (!err.empty())
            update_status_bar("Sync failed: " + err);
        else if (!m_agent->is_signed_in())
            update_status_bar("Not signed in. Click Login.");
        else
            update_status_bar("No printers registered on the server yet.");
    } else {
        update_status_bar("Showing " + std::to_string(m_printers.size()) + " printers.");
    }
}

void ForgeFleetPanel::update_status_bar(const std::string& msg)
{
    if (m_status_label) m_status_label->SetLabel(wxString::FromUTF8(msg));
}

}} // namespace Slic3r::GUI

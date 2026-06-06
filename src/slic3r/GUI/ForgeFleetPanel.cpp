#include "ForgeFleetPanel.hpp"
#include "ForgeControlPanel.hpp"
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

namespace Slic3r { namespace GUI {

namespace {
constexpr int POLL_INTERVAL_MS = 5000;
constexpr int COL_NAME   = 0;
constexpr int COL_STATUS = 1;
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

    // Native-style section title bar (matches StatusPanel's STATUS_TITLE_BG bars
    // on the Bambu Device monitor): a light header strip with a bold grey label.
    auto make_title = [this](const wxString& text) {
        auto* bar = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(34)));
        bar->SetBackgroundColour(wxColour(248, 248, 248));
        auto* s = new wxBoxSizer(wxHORIZONTAL);
        auto* lbl = new wxStaticText(bar, wxID_ANY, text);
        auto lf = lbl->GetFont(); lf.MakeBold(); lbl->SetFont(lf);
        lbl->SetForegroundColour(wxColour(107, 107, 107));
        s->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
        bar->SetSizer(s);
        return bar;
    };

    auto* body = new wxBoxSizer(wxHORIZONTAL);

    // ---- Left column: printer picker + account / refresh ----
    auto* leftcol = new wxBoxSizer(wxVERTICAL);
    leftcol->Add(make_title(_L("Printers")), 0, wxEXPAND);

    m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL);
    m_list->AppendColumn(_L("Printer"), wxLIST_FORMAT_LEFT, FromDIP(110));
    m_list->AppendColumn(_L("Status"),  wxLIST_FORMAT_LEFT, FromDIP(70));
    m_list->SetMinSize(wxSize(FromDIP(190), FromDIP(420)));
    leftcol->Add(m_list, 1, wxEXPAND | wxTOP, FromDIP(2));

    m_status_label = new wxStaticText(this, wxID_ANY, _L("Not signed in."));
    leftcol->Add(m_status_label, 0, wxLEFT | wxTOP, FromDIP(6));
    m_server_label = new wxStaticText(this, wxID_ANY,
        wxString::Format(_L("Server: %s"), wxString::FromUTF8(m_agent->server_url())));
    { auto sf = m_server_label->GetFont(); sf.SetPointSize(sf.GetPointSize() - 1); m_server_label->SetFont(sf); }
    leftcol->Add(m_server_label, 0, wxLEFT | wxTOP | wxBOTTOM, FromDIP(4));

    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    m_btn_login     = new wxButton(this, wxID_ANY, _L("Login..."));
    m_btn_configure = new wxButton(this, wxID_ANY, _L("Server URL..."));
    m_btn_refresh   = new wxButton(this, wxID_ANY, _L("Refresh"));
    m_btn_print     = new wxButton(this, wxID_ANY, _L("Send G-code"));
    btn_row->Add(m_btn_login,   0, wxRIGHT, 4);
    btn_row->Add(m_btn_refresh, 0, wxRIGHT, 4);
    btn_row->Add(m_btn_configure, 0);
    leftcol->Add(btn_row, 0, wxTOP, FromDIP(4));
    leftcol->Add(m_btn_print, 0, wxTOP, FromDIP(4));
    body->Add(leftcol, 0, wxEXPAND | wxRIGHT, FromDIP(10));

    // ---- Center column: camera + printing progress ----
    auto* detail = new wxBoxSizer(wxVERTICAL);
    detail->Add(make_title(_L("Camera")), 0, wxEXPAND);
    m_camera = new wxStaticBitmap(this, wxID_ANY, wxBitmap());
    m_camera->SetMinSize(wxSize(FromDIP(240), FromDIP(320)));
    detail->Add(m_camera, 1, wxEXPAND | wxTOP, FromDIP(2));
    detail->Add(make_title(_L("Printing progress")), 0, wxEXPAND | wxTOP, FromDIP(8));
    m_detail_label = new wxStaticText(this, wxID_ANY, _L("Select a printer to see details."));
    detail->Add(m_detail_label, 0, wxTOP | wxLEFT, FromDIP(6));

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

    // Control panel — the REAL native Bambu Device "Control" widgets (temp
    // column, AxisCtrlButton dial, bed Z, extruder, fan/lamp/speed), reused
    // for every brand and wired to the dashboard. Shown only for Klipper/
    // Moonraker printers (gcode control); Bambu keeps its own monitor tab.
    ForgeControlPanel::Callbacks cb;
    cb.move    = [this](const std::string& ax, double d) { if (!m_selected_printer_id.empty()) m_agent->control_move(m_selected_printer_id, ax, d); };
    cb.home    = [this]()                                 { if (!m_selected_printer_id.empty()) m_agent->control_home(m_selected_printer_id); };
    cb.extrude = [this](double amt)                       { if (!m_selected_printer_id.empty()) m_agent->control_extrude(m_selected_printer_id, amt); };
    cb.set_temp= [this](const std::string& h, int t, int tool) { if (!m_selected_printer_id.empty()) m_agent->control_set_temp(m_selected_printer_id, h, t, tool); };
    cb.fan     = [this](int pct)                          { if (!m_selected_printer_id.empty()) m_agent->control_fan(m_selected_printer_id, pct); };
    cb.light   = [this](bool on)                          { if (!m_selected_printer_id.empty()) m_agent->control_light(m_selected_printer_id, on); };
    cb.speed   = [this](int pct)                          { if (!m_selected_printer_id.empty()) m_agent->control_speed(m_selected_printer_id, pct); };
    cb.select_tool = [this](int tool)                     { if (!m_selected_printer_id.empty()) m_agent->control_tool(m_selected_printer_id, tool); };

    m_control = new ForgeControlPanel(this, std::move(cb));
    m_control->Hide();

    body->Add(detail, 1, wxEXPAND | wxRIGHT, FromDIP(10));

    // ---- Right column: "Control" title bar + the native control widgets ----
    auto* rightcol = new wxBoxSizer(wxVERTICAL);
    rightcol->Add(make_title(_L("Control")), 0, wxEXPAND);
    rightcol->Add(m_control, 1, wxEXPAND | wxTOP, FromDIP(2));
    body->Add(rightcol, 0, wxEXPAND);

    root->Add(body, 1, wxALL | wxEXPAND, FromDIP(12));

    SetSizer(root);
    if (getenv("FORGE_OPEN_DEVICES")) m_control->Show(true); // dev/QA aid: show Control without a selection

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
    if (m_control && m_control->IsShown() != gcode_capable) {
        m_control->Show(gcode_capable);
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

    // Push live telemetry into the native Control widgets (temps, tools).
    if (m_control && gcode_capable && ls.ok) m_control->update_state(ls);

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
        wxString status = p.status.empty() ? wxString::FromUTF8(p.state) : wxString::FromUTF8(p.status);
        if (p.progress_pct > 0) status += wxString::Format(" %d%%", p.progress_pct);
        m_list->SetItem(row, COL_STATUS, status);
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

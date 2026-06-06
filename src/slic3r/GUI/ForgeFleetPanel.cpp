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

namespace Slic3r { namespace GUI {

namespace {
constexpr int POLL_INTERVAL_MS = 5000;
constexpr int COL_NAME     = 0;
constexpr int COL_VENDOR   = 1;
constexpr int COL_HOST     = 2;
constexpr int COL_STATUS   = 3;
constexpr int COL_PROGRESS = 4;
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

    // Printer list on the left, per-printer detail + camera on the right.
    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(m_list, 2, wxEXPAND | wxRIGHT, 10);

    auto* detail = new wxBoxSizer(wxVERTICAL);
    m_detail_label = new wxStaticText(this, wxID_ANY, _L("Select a printer to see details."));
    detail->Add(m_detail_label, 0, wxBOTTOM, 8);
    m_camera = new wxStaticBitmap(this, wxID_ANY, wxBitmap());
    m_camera->SetMinSize(wxSize(320, 240));
    detail->Add(m_camera, 1, wxEXPAND);

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

    // Motion / tool controls — shown only for Klipper/Moonraker printers
    // (jog/extrude/home/tool-select route to gcode via the dashboard).
    m_motion_panel = new wxPanel(this, wxID_ANY);
    {
        auto* mp = new wxBoxSizer(wxVERTICAL);
        auto jog = [this](const std::string& ax, double d) {
            if (!m_selected_printer_id.empty()) m_agent->control_move(m_selected_printer_id, ax, d);
        };

        auto* tools = new wxBoxSizer(wxHORIZONTAL);
        tools->Add(new wxStaticText(m_motion_panel, wxID_ANY, _L("Tools:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        for (int t = 0; t < 4; ++t) {
            auto* b = new wxButton(m_motion_panel, wxID_ANY, wxString::Format("T%d", t + 1), wxDefaultPosition, wxSize(40, -1));
            b->Bind(wxEVT_BUTTON, [this, t](wxCommandEvent&) {
                if (!m_selected_printer_id.empty()) m_agent->control_tool(m_selected_printer_id, t);
            });
            tools->Add(b, 0, wxRIGHT, 4);
        }
        mp->Add(tools, 0, wxBOTTOM, 6);

        auto* jrow = new wxBoxSizer(wxHORIZONTAL);
        auto* home = new wxButton(m_motion_panel, wxID_ANY, _L("Home"));
        home->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!m_selected_printer_id.empty()) m_agent->control_home(m_selected_printer_id);
        });
        jrow->Add(home, 0, wxRIGHT, 10);
        struct J { const char* lbl; const char* ax; double d; };
        for (const J& j : { J{"X-","X",-10}, J{"X+","X",10}, J{"Y-","Y",-10}, J{"Y+","Y",10}, J{"Z-","Z",-10}, J{"Z+","Z",10} }) {
            auto* b = new wxButton(m_motion_panel, wxID_ANY, j.lbl, wxDefaultPosition, wxSize(40, -1));
            std::string ax = j.ax; double d = j.d;
            b->Bind(wxEVT_BUTTON, [jog, ax, d](wxCommandEvent&) { jog(ax, d); });
            jrow->Add(b, 0, wxRIGHT, 4);
        }
        mp->Add(jrow, 0, wxBOTTOM, 6);

        auto* erow = new wxBoxSizer(wxHORIZONTAL);
        auto* ext = new wxButton(m_motion_panel, wxID_ANY, _L("Extrude"));
        auto* ret = new wxButton(m_motion_panel, wxID_ANY, _L("Retract"));
        ext->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (!m_selected_printer_id.empty()) m_agent->control_extrude(m_selected_printer_id, 5);  });
        ret->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (!m_selected_printer_id.empty()) m_agent->control_extrude(m_selected_printer_id, -5); });
        erow->Add(ext, 0, wxRIGHT, 4);
        erow->Add(ret, 0);
        mp->Add(erow, 0);

        m_motion_panel->SetSizer(mp);
    }
    m_motion_panel->Hide();
    detail->Add(m_motion_panel, 0, wxTOP, 10);

    body->Add(detail, 1, wxEXPAND);

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
        wxString temps;
        if (ls.nozzle_temp >= 0)  temps += wxString::Format(_L("Nozzle %.0f°C  "), ls.nozzle_temp);
        if (ls.bed_temp >= 0)     temps += wxString::Format(_L("Bed %.0f°C  "), ls.bed_temp);
        if (ls.chamber_temp >= 0) temps += wxString::Format(_L("Chamber %.0f°C"), ls.chamber_temp);
        if (!temps.empty()) info += "\n" + temps;
    }
    m_detail_label->SetLabel(info);

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

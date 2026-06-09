#include "ForgeFleetPanel.hpp"
#include "ForgeControlPanel.hpp"
#include "ForgeCloud.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "PartPlate.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include <boost/filesystem.hpp>

#include <wx/utils.h>

#include "libslic3r/AppConfig.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/dialog.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/statbmp.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/filedlg.h>
#include <wx/file.h>

#include <cctype>

#include "Widgets/ProgressBar.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StepCtrl.hpp"
#include "Widgets/StateColor.hpp"

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
    m_btn_add       = new wxButton(this, wxID_ANY, _L("Add printer..."));
    m_btn_remove    = new wxButton(this, wxID_ANY, _L("Remove"));
    btn_row->Add(m_btn_login,   0, wxRIGHT, 4);
    btn_row->Add(m_btn_refresh, 0, wxRIGHT, 4);
    btn_row->Add(m_btn_configure, 0);
    leftcol->Add(btn_row, 0, wxTOP, FromDIP(4));
    auto* btn_row2 = new wxBoxSizer(wxHORIZONTAL);
    btn_row2->Add(m_btn_add,    0, wxRIGHT, 4);
    btn_row2->Add(m_btn_remove, 0);
    leftcol->Add(btn_row2, 0, wxTOP, FromDIP(4));
    leftcol->Add(m_btn_print, 0, wxTOP, FromDIP(4));
    body->Add(leftcol, 0, wxEXPAND | wxRIGHT, FromDIP(10));

    // ---- Center column: camera + printing progress ----
    auto* detail = new wxBoxSizer(wxVERTICAL);

    // Camera title bar with a small toolbar (refresh / snapshot / open stream),
    // mirroring the Bambu Device camera header's icon row.
    auto* cam_title = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(34)));
    cam_title->SetBackgroundColour(wxColour(248, 248, 248));
    auto* cam_ts = new wxBoxSizer(wxHORIZONTAL);
    { auto* l = new wxStaticText(cam_title, wxID_ANY, _L("Camera"));
      auto lf = l->GetFont(); lf.MakeBold(); l->SetFont(lf); l->SetForegroundColour(wxColour(107, 107, 107));
      cam_ts->Add(l, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12)); }
    cam_ts->AddStretchSpacer(1);
    auto add_cam_btn = [&](const wxString& label, std::function<void()> fn) {
        auto* b = new wxButton(cam_title, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        b->Bind(wxEVT_BUTTON, [fn](wxCommandEvent&) { fn(); });
        cam_ts->Add(b, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    };
    add_cam_btn(_L("Refresh"),  [this] { update_detail(); });
    add_cam_btn(_L("Snapshot"), [this] { save_camera_snapshot(); });
    add_cam_btn(_L("Open"),     [this] { if (!m_agent->server_url().empty()) wxLaunchDefaultBrowser(wxString::FromUTF8(m_agent->server_url())); });
    cam_title->SetSizer(cam_ts);
    detail->Add(cam_title, 0, wxEXPAND);

    m_camera = new wxStaticBitmap(this, wxID_ANY, wxBitmap());
    m_camera->SetMinSize(wxSize(FromDIP(240), FromDIP(320)));
    detail->Add(m_camera, 1, wxEXPAND | wxTOP, FromDIP(2));

    // HMS / fault banner — a red strip shown only when the printer reports an
    // error message (mirrors the Bambu Device error notification).
    m_error_banner = new wxPanel(this, wxID_ANY);
    m_error_banner->SetBackgroundColour(wxColour(0xC8, 0x37, 0x37));
    { auto* es = new wxBoxSizer(wxHORIZONTAL);
      m_error_text = new wxStaticText(m_error_banner, wxID_ANY, "");
      m_error_text->SetForegroundColour(*wxWHITE);
      es->Add(m_error_text, 1, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(6));
      m_error_banner->SetSizer(es); }
    m_error_banner->Hide();
    detail->Add(m_error_banner, 0, wxEXPAND | wxTOP, FromDIP(4));

    detail->Add(make_title(_L("Printing progress")), 0, wxEXPAND | wxTOP, FromDIP(8));

    // Print-task card: job name, stage + percent, progress bar, layer/time/speed.
    m_job_name = new wxStaticText(this, wxID_ANY, _L("Select a printer to see details."),
                                  wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    { auto jf = m_job_name->GetFont(); jf.MakeBold(); m_job_name->SetFont(jf); }
    detail->Add(m_job_name, 0, wxTOP | wxLEFT | wxRIGHT, FromDIP(6));

    auto* stage_row = new wxBoxSizer(wxHORIZONTAL);
    m_stage_label = new wxStaticText(this, wxID_ANY, "");
    m_stage_label->SetForegroundColour(wxColour(146, 146, 146));
    m_progress_pct = new wxStaticText(this, wxID_ANY, "");
    { auto pf = m_progress_pct->GetFont(); pf.MakeBold(); m_progress_pct->SetFont(pf); }
    stage_row->Add(m_stage_label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
    stage_row->Add(m_progress_pct, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    detail->Add(stage_row, 0, wxEXPAND | wxTOP, FromDIP(4));

    m_progress_bar = new ProgressBar(this, wxID_ANY, 100);
    m_progress_bar->SetHeight(FromDIP(8));
    m_progress_bar->SetProgressForedColour(wxColour(0x00, 0x97, 0x89)); // brand teal
    m_progress_bar->SetMinSize(wxSize(FromDIP(240), FromDIP(8)));
    detail->Add(m_progress_bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

    m_layer_time = new wxStaticText(this, wxID_ANY, "");
    m_layer_time->SetForegroundColour(wxColour(146, 146, 146));
    detail->Add(m_layer_time, 0, wxTOP | wxLEFT, FromDIP(4));

    // Generic print-stage flow (analog of Bambu Device's calibration step
    // indicator): a checkmarked step list driven by gcode_state/progress/temps.
    m_stage_steps = new StepIndicator(this, wxID_ANY);
    m_stage_steps->SetBackgroundColor(StateColor(std::make_pair(GetBackgroundColour(), (int) StateColor::Normal)));
    m_stage_steps->SetFont(::Label::Body_12);
    for (const wxString& s : { _L("Prepare"), _L("Heating"), _L("Printing"), _L("Finishing"), _L("Done") })
        m_stage_steps->AppendItem(s);
    m_stage_steps->SetMinSize(wxSize(FromDIP(220), FromDIP(150)));
    m_stage_steps->Hide();
    detail->Add(m_stage_steps, 0, wxTOP | wxLEFT, FromDIP(6));

    // Kept for misc one-off status text (errors before a printer is picked).
    m_detail_label = new wxStaticText(this, wxID_ANY, "");
    detail->Add(m_detail_label, 0, wxTOP | wxLEFT, FromDIP(4));

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
        // Load / Unload / Change filament for the active toolhead (M701/M702/M600).
        fr->AddSpacer(FromDIP(6));
        m_btn_load   = new wxButton(m_filament_row, wxID_ANY, _L("Load"),   wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_btn_unload = new wxButton(m_filament_row, wxID_ANY, _L("Unload"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_btn_change = new wxButton(m_filament_row, wxID_ANY, _L("Change"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        auto fil = [this](const char* op) {
            if (!m_selected_printer_id.empty()) m_agent->control_filament(m_selected_printer_id, op, -1);
        };
        m_btn_load  ->Bind(wxEVT_BUTTON, [fil](wxCommandEvent&) { fil("load"); });
        m_btn_unload->Bind(wxEVT_BUTTON, [fil](wxCommandEvent&) { fil("unload"); });
        m_btn_change->Bind(wxEVT_BUTTON, [fil](wxCommandEvent&) { fil("change"); });
        fr->Add(m_btn_load,   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        fr->Add(m_btn_unload, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        fr->Add(m_btn_change, 0, wxALIGN_CENTER_VERTICAL);
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

    // Control title bar with native-style action buttons (Printer Parts /
    // Print Options / Calibration), mirroring the Bambu Device control header.
    auto* ctrl_title = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(34)));
    ctrl_title->SetBackgroundColour(wxColour(248, 248, 248));
    auto* cts = new wxBoxSizer(wxHORIZONTAL);
    auto* clbl = new wxStaticText(ctrl_title, wxID_ANY, _L("Control"));
    { auto lf = clbl->GetFont(); lf.MakeBold(); clbl->SetFont(lf); clbl->SetForegroundColour(wxColour(107, 107, 107)); }
    cts->Add(clbl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
    cts->AddStretchSpacer(1);
    auto add_hdr_btn = [&](const wxString& label, std::function<void()> fn) {
        auto* b = new wxButton(ctrl_title, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        b->Bind(wxEVT_BUTTON, [fn](wxCommandEvent&) { fn(); });
        cts->Add(b, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    };
    add_hdr_btn(_L("Printer Parts"),  [this] { if (!m_agent->server_url().empty()) wxLaunchDefaultBrowser(wxString::FromUTF8(m_agent->server_url())); });
    add_hdr_btn(_L("Print Options"),  [this] { if (!m_agent->server_url().empty()) wxLaunchDefaultBrowser(wxString::FromUTF8(m_agent->server_url())); });
    add_hdr_btn(_L("Safety Options"), [this] { if (!m_agent->server_url().empty()) wxLaunchDefaultBrowser(wxString::FromUTF8(m_agent->server_url())); });
    add_hdr_btn(_L("Calibration"),    [] { if (wxGetApp().mainframe) wxGetApp().mainframe->select_tab(size_t(MainFrame::tpCalibration)); });
    ctrl_title->SetSizer(cts);

    rightcol->Add(ctrl_title, 0, wxEXPAND);
    rightcol->Add(m_control, 1, wxEXPAND | wxTOP, FromDIP(2));
    body->Add(rightcol, 0, wxEXPAND);

    root->Add(body, 1, wxALL | wxEXPAND, FromDIP(12));

    SetSizer(root);
    if (getenv("FORGE_OPEN_DEVICES")) m_control->Show(true); // dev/QA aid: show Control without a selection

    m_btn_login    ->Bind(wxEVT_BUTTON, &ForgeFleetPanel::on_login, this);
    m_btn_configure->Bind(wxEVT_BUTTON, &ForgeFleetPanel::on_configure, this);
    m_btn_refresh  ->Bind(wxEVT_BUTTON, &ForgeFleetPanel::on_refresh, this);
    m_btn_print    ->Bind(wxEVT_BUTTON, &ForgeFleetPanel::on_print, this);
    m_btn_add      ->Bind(wxEVT_BUTTON, &ForgeFleetPanel::on_add_printer, this);
    m_btn_remove   ->Bind(wxEVT_BUTTON, &ForgeFleetPanel::on_remove_printer, this);
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

    // One-time correction: GTK auto-selects row 0 when the list is populated,
    // which may land on a non-controllable (e.g. Bambu) printer. The very first
    // selection is always that programmatic one — if it picked a non-gcode
    // printer while a gcode one exists, redirect to the controllable printer so
    // the Control panel + filament row are visible. Real user clicks (every
    // selection after this first one) are always respected.
    if (!m_corrected_initial) {
        m_corrected_initial = true;
        auto is_gcode = [](const ForgePrinter& p) { return p.vendor == "moonraker" || p.vendor == "klipper"; };
        if (idx >= 0 && idx < (long)m_printers.size() && !is_gcode(m_printers[idx])) {
            for (size_t i = 0; i < m_printers.size(); ++i)
                if (is_gcode(m_printers[i])) {
                    m_selected_printer_id = m_printers[i].id;
                    if (m_list) m_list->SetItemState((long) i, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                                     wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
                    break;
                }
        }
    }
    update_detail();
}

void ForgeFleetPanel::update_detail()
{
    if (m_selected_printer_id.empty() || !m_job_name) {
        if (m_job_name)     m_job_name->SetLabel(_L("Select a printer to see details."));
        if (m_stage_label)  m_stage_label->SetLabel("");
        if (m_progress_pct) m_progress_pct->SetLabel("");
        if (m_layer_time)   m_layer_time->SetLabel("");
        if (m_progress_bar) m_progress_bar->SetProgress(0);
        if (m_stage_steps && m_stage_steps->IsShown()) m_stage_steps->Hide();
        if (m_detail_label) m_detail_label->SetLabel("");
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

    // Live runtime telemetry — best-effort, read-only.
    ForgeLiveState ls = m_agent->get_printer_state(m_selected_printer_id);

    auto fmt_dur = [](int secs) -> wxString {
        if (secs < 0) return wxString();
        int h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
        if (h > 0) return wxString::Format("%dh %dm", h, m);
        if (m > 0) return wxString::Format("%dm %ds", m, s);
        return wxString::Format("%ds", s);
    };

    // --- Print-task card ---
    // Job name: prefer the live sub-task, strip extension; fall back to roster.
    wxString job = ls.ok && !ls.job_name.empty() ? wxString::FromUTF8(ls.job_name)
                                                 : wxString::FromUTF8(p->current_job);
    { int dot = job.rfind('.'); if (dot != (int)wxString::npos && dot > (int)job.size() - 8) job = job.Left(dot); }
    if (job.empty()) job = wxString::FromUTF8(p->name);
    m_job_name->SetLabel(job);

    wxString stage = ls.ok && !ls.stage_label.empty() ? wxString::FromUTF8(ls.stage_label)
                   : ls.ok && !ls.state.empty()       ? wxString::FromUTF8(ls.state)
                   : (p->status.empty() ? wxString::FromUTF8(p->state) : wxString::FromUTF8(p->status));
    m_stage_label->SetLabel(stage);

    int pct = ls.ok && ls.progress_pct >= 0 ? ls.progress_pct : p->progress_pct;
    m_progress_pct->SetLabel(pct >= 0 ? wxString::Format("%d%%", pct) : wxString());
    if (m_progress_bar) m_progress_bar->SetProgress(pct >= 0 ? pct : 0);

    // Layer x/y · time left · speed · filament used
    wxString meta;
    auto sep = [&meta]() { if (!meta.empty()) meta += "   ·   "; };
    if (ls.ok && ls.layer_total > 0) { sep(); meta += wxString::Format(_L("Layer %d/%d"), ls.layer_cur, ls.layer_total); }
    if (ls.ok && ls.time_total > 0 && ls.time_elapsed >= 0) {
        wxString left = fmt_dur(ls.time_total - ls.time_elapsed);
        if (!left.empty()) { sep(); meta += left + " " + _L("left"); }
    }
    if (ls.ok && ls.speed_pct > 0)        { sep(); meta += wxString::Format("%d%%", ls.speed_pct); }
    if (ls.ok && ls.filament_used_mm > 0) { sep(); meta += wxString::Format(_L("%.1f m"), ls.filament_used_mm / 1000.0); }
    m_layer_time->SetLabel(meta);

    // Generic print-stage step flow (Prepare → Heating → Printing → Finishing →
    // Done), derived from gcode_state + progress + heater deltas. Hidden when the
    // printer is plain idle with no job context.
    if (m_stage_steps) {
        std::string st = ls.ok ? ls.state : std::string();
        for (char& c : st) c = (char) ::toupper((unsigned char) c);
        const int prog = ls.ok && ls.progress_pct >= 0 ? ls.progress_pct : p->progress_pct;
        bool heating = false;
        if (ls.ok && prog <= 0)
            for (const auto& t : ls.tools)
                if (t.target > 0 && t.temp >= 0 && t.temp < t.target - 3) { heating = true; break; }
        int step = -1;
        if (st == "PREPARE" || st == "PREPARING" || st == "SLICING" || st == "INIT") step = 0;
        else if (heating)                                                             step = 1;
        else if (prog >= 100 || st == "FINISH" || st == "COMPLETE" || st == "COMPLETED") step = 4;
        else if (prog >= 98)                                                          step = 3;
        else if (prog > 0 || st == "RUNNING" || st == "PRINTING" || st == "PAUSE" || st == "PAUSED") step = 2;
        const bool show = step >= 0;
        if (show) m_stage_steps->SelectItem(step);
        if (m_stage_steps->IsShown() != show) { m_stage_steps->Show(show); Layout(); }
    }

    // Compact temps line (kept in the small detail label).
    wxString temps;
    if (ls.ok && ls.bed_temp >= 0)     temps += wxString::Format(_L("Bed %.0f°C  "), ls.bed_temp);
    if (ls.ok && ls.chamber_temp >= 0) temps += wxString::Format(_L("Chamber %.0f°C"), ls.chamber_temp);
    m_detail_label->SetLabel(temps);

    // HMS / fault banner.
    if (m_error_banner) {
        const bool has_err = ls.ok && !ls.error_msg.empty();
        if (has_err) m_error_text->SetLabel(wxString::FromUTF8(ls.error_msg));
        if (m_error_banner->IsShown() != has_err) { m_error_banner->Show(has_err); Layout(); }
    }

    // Push live telemetry into the native Control widgets (temps, tools).
    if (m_control && gcode_capable && ls.ok) m_control->update_state(ls);

    // Update the filament slots — color swatch + material per toolhead.
    // Shown only for multi-tool printers; the active tool's label is bold.
    if (m_filament_row) {
        const int ntools = (int)ls.tools.size();
        // Row (with Load/Unload/Change) is available for any gcode printer;
        // individual colour swatches show per detected toolhead.
        const bool show_slots = gcode_capable;
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
    m_last_frame = jpeg;   // cache for the Snapshot button
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

void ForgeFleetPanel::save_camera_snapshot()
{
    if (m_last_frame.empty()) {
        wxMessageBox(_L("No camera frame available yet."), _L("Snapshot"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxString def = wxString::Format("%s_snapshot.jpg",
                                    m_selected_printer_id.empty() ? "printer" : wxString::FromUTF8(m_selected_printer_id));
    wxFileDialog dlg(this, _L("Save camera snapshot"), "", def,
                     "JPEG image (*.jpg)|*.jpg", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    wxFile f(dlg.GetPath(), wxFile::write);
    if (f.IsOpened()) f.Write(m_last_frame.data(), m_last_frame.size());
}

void ForgeFleetPanel::on_show()
{
    refresh_printer_list();

    // Auto-select a printer so the camera + Control panel populate instead of
    // showing an empty view. Prefer a gcode-capable (Moonraker/Klipper) printer
    // so the control widgets light up. Deferred via CallAfter so it runs *after*
    // GTK's own initial row-0 selection (which would otherwise win the race and
    // leave a non-controllable printer selected).
    CallAfter([this] {
        if (m_printers.empty()) return;
        auto is_gcode = [](const ForgePrinter& p) {
            return p.vendor == "moonraker" || p.vendor == "klipper";
        };
        const ForgePrinter* cur = nullptr;
        for (const auto& fp : m_printers)
            if (fp.id == m_selected_printer_id) { cur = &fp; break; }
        if (cur && is_gcode(*cur)) return;           // already on a controllable printer
        long idx = -1;
        for (size_t i = 0; i < m_printers.size(); ++i)
            if (is_gcode(m_printers[i])) { idx = (long) i; break; }
        if (idx < 0 && m_selected_printer_id.empty()) idx = 0; // no gcode printer: show first
        if (idx < 0) return;
        m_selected_printer_id = m_printers[idx].id;
        if (m_list) m_list->SetItemState(idx, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                         wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        update_detail();
    });

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
    const std::string printer_id   = m_printers[sel].id;
    const wxString    printer_name = wxString::FromUTF8(m_printers[sel].name);

    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;

    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    if (plate == nullptr || !plate->is_slice_result_valid()) {
        wxMessageBox(_L("Slice the plate first, then send it to the printer."),
                     _L("3DPrintForge Devices"), wxICON_INFORMATION);
        return;
    }
    const int plate_idx = plater->get_partplate_list().get_curr_plate_index();

    // Export the sliced plate to a temporary .gcode.3mf, then upload it to the
    // dashboard which routes it to the chosen printer (any brand) and queues it.
    namespace fs = boost::filesystem;
    wxString fname_wx = plater->get_export_gcode_filename(".gcode.3mf", /*only_filename*/ true);
    std::string fname = fname_wx.empty() ? std::string("print.gcode.3mf") : std::string(fname_wx.ToUTF8());
    fs::path out_path = fs::temp_directory_path() / ("forge_" + std::to_string(plate_idx) + "_" + fname);

    {
        wxBusyCursor wait;
        update_status_bar("Exporting G-code...");
        plater->export_3mf(out_path,
                           SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::WithGcode,
                           plate_idx);
    }
    boost::system::error_code ec;
    if (!fs::exists(out_path, ec) || fs::file_size(out_path, ec) == 0) {
        wxMessageBox(_L("Could not export the sliced file."),
                     _L("3DPrintForge Devices"), wxICON_ERROR);
        return;
    }

    CloudProvider* prov = cloud_provider("3dprintforge");
    if (prov == nullptr) {
        wxMessageBox(_L("3DPrintForge provider unavailable."),
                     _L("3DPrintForge Devices"), wxICON_ERROR);
        fs::remove(out_path, ec);
        return;
    }

    CloudJobResult res;
    {
        wxBusyCursor wait;
        update_status_bar("Sending to " + std::string(printer_name.ToUTF8()) + "...");
        res = prov->send_job(out_path.string(), fname, printer_id, /*auto_queue*/ true);
    }
    fs::remove(out_path, ec);

    if (res.ok) {
        update_status_bar("Sent to " + std::string(printer_name.ToUTF8()));
        wxMessageBox(wxString::Format(_L("Sent to %s and queued."), printer_name),
                     _L("3DPrintForge Devices"), wxICON_INFORMATION);
    } else {
        wxMessageBox(wxString::Format(_L("Send failed: %s"), wxString::FromUTF8(res.message)),
                     _L("3DPrintForge Devices"), wxICON_ERROR);
    }
}

void ForgeFleetPanel::on_add_printer(wxCommandEvent& /*evt*/)
{
    // Small modal form: name + type + IP, plus Bambu-only serial/access code.
    // The dashboard stores the connection and handles the per-brand protocol.
    wxDialog dlg(this, wxID_ANY, _L("Add printer"), wxDefaultPosition, wxSize(420, -1));
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* grid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
    grid->AddGrowableCol(1, 1);

    auto add_row = [&](const wxString& label, wxWindow* ctrl) {
        grid->Add(new wxStaticText(&dlg, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    auto* name_ctrl = new wxTextCtrl(&dlg, wxID_ANY);
    auto* type_ctrl = new wxChoice(&dlg, wxID_ANY);
    const char* types[] = { "bambu", "moonraker", "klipper", "prusalink",
                            "creality", "elegoo", "anker", "voron", "ratrig", "qidi" };
    for (const char* t : types) type_ctrl->Append(wxString::FromUTF8(t));
    type_ctrl->SetSelection(0);
    auto* ip_ctrl     = new wxTextCtrl(&dlg, wxID_ANY);
    ip_ctrl->SetHint("192.168.x.x");
    auto* model_ctrl  = new wxTextCtrl(&dlg, wxID_ANY);
    auto* serial_ctrl = new wxTextCtrl(&dlg, wxID_ANY);
    auto* access_ctrl = new wxTextCtrl(&dlg, wxID_ANY);

    add_row(_L("Name:"),        name_ctrl);
    add_row(_L("Type:"),        type_ctrl);
    add_row(_L("IP address:"),  ip_ctrl);
    add_row(_L("Model:"),       model_ctrl);
    add_row(_L("Serial (Bambu):"),      serial_ctrl);
    add_row(_L("Access code (Bambu):"), access_ctrl);

    // Bambu-only fields are only relevant for type == bambu.
    auto sync_bambu = [&]() {
        const bool bambu = type_ctrl->GetStringSelection() == "bambu";
        serial_ctrl->Enable(bambu);
        access_ctrl->Enable(bambu);
    };
    sync_bambu();
    type_ctrl->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) { sync_bambu(); });

    root->Add(grid, 1, wxEXPAND | wxALL, FromDIP(12));
    auto* btns = new wxBoxSizer(wxHORIZONTAL);
    btns->AddStretchSpacer();
    btns->Add(new wxButton(&dlg, wxID_OK,     _L("Add")),    0, wxRIGHT, 8);
    btns->Add(new wxButton(&dlg, wxID_CANCEL, _L("Cancel")), 0);
    root->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    dlg.SetSizerAndFit(root);

    if (dlg.ShowModal() != wxID_OK) return;

    ForgePrinterConfig cfg;
    cfg.name        = name_ctrl->GetValue().ToStdString();
    cfg.type        = type_ctrl->GetStringSelection().ToStdString();
    cfg.ip          = ip_ctrl->GetValue().ToStdString();
    cfg.model       = model_ctrl->GetValue().ToStdString();
    if (cfg.type == "bambu") {
        cfg.serial      = serial_ctrl->GetValue().ToStdString();
        cfg.access_code = access_ctrl->GetValue().ToStdString();
    }
    if (cfg.name.empty()) {
        wxMessageBox(_L("A name is required."), _L("Add printer"), wxICON_INFORMATION);
        return;
    }

    update_status_bar("Adding printer...");
    auto id = m_agent->add_printer(cfg);
    if (id.has_value()) {
        update_status_bar("Added " + cfg.name);
        refresh_printer_list();
    } else {
        wxMessageBox(wxString::Format(_L("Could not add the printer: %s"),
                                      wxString::FromUTF8(m_agent->auth_state().last_error)),
                     _L("Add printer"), wxICON_ERROR);
    }
}

void ForgeFleetPanel::on_remove_printer(wxCommandEvent& /*evt*/)
{
    long sel = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= (long)m_printers.size()) {
        wxMessageBox(_L("Pick a printer first."), _L("3DPrintForge Devices"), wxICON_INFORMATION);
        return;
    }
    const std::string id   = m_printers[sel].id;
    const wxString     name = wxString::FromUTF8(m_printers[sel].name);
    if (wxMessageBox(wxString::Format(_L("Remove printer \"%s\" from the fleet?"), name),
                     _L("Remove printer"), wxYES_NO | wxICON_QUESTION) != wxYES)
        return;
    if (m_agent->delete_printer(id)) {
        update_status_bar("Removed " + std::string(name.ToUTF8()));
        refresh_printer_list();
    } else {
        wxMessageBox(wxString::Format(_L("Could not remove the printer: %s"),
                                      wxString::FromUTF8(m_agent->auth_state().last_error)),
                     _L("Remove printer"), wxICON_ERROR);
    }
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
        // Universe: make sure each connected printer's slicing preset is installed
        // so it also appears under "Printer". Only installs ones not already present
        // (and only reloads presets when something new was added).
        const int added = forge_sync_fleet_to_presets();
        if (added > 0)
            update_status_bar("Showing " + std::to_string(m_printers.size()) +
                              " printers — added " + std::to_string(added) + " to the Printer list.");
    }
}

void ForgeFleetPanel::update_status_bar(const std::string& msg)
{
    if (m_status_label) m_status_label->SetLabel(wxString::FromUTF8(msg));
}

}} // namespace Slic3r::GUI

#pragma once

#include <wx/panel.h>
#include <wx/timer.h>

#include "../Utils/ForgeCloudAgent.hpp"

#include <memory>
#include <vector>

class wxListCtrl;
class wxStaticText;
class wxButton;
class wxTextCtrl;
class wxStaticBitmap;
class wxListEvent;
class ProgressBar;
class StepIndicator;

namespace Slic3r { namespace GUI {

class ForgeControlPanel;

// Multi-brand fleet panel — replaces (or sits alongside) OrcaSlicer's
// Bambu-only Device tab. Reads the printer roster from the user's
// 3DPrintForge Server and lets them queue prints, refresh status,
// and connect a session.
class ForgeFleetPanel : public wxPanel {
public:
    ForgeFleetPanel(wxWindow* parent);
    ~ForgeFleetPanel() override;

    // Called when the panel becomes visible — kicks a refresh and
    // starts the polling timer. Stopping is handled in OnHide.
    void on_show();
    void on_hide();

    ForgeCloudAgent* agent() { return m_agent.get(); }

private:
    void build_ui();
    void on_refresh(wxCommandEvent& evt);
    void on_configure(wxCommandEvent& evt);
    void on_login(wxCommandEvent& evt);
    void on_print(wxCommandEvent& evt);
    void on_timer(wxTimerEvent& evt);

    void refresh_printer_list();
    void update_status_bar(const std::string& msg);
    void on_select(wxListEvent& evt);
    void update_detail();   // refresh the selected printer's info + camera
    void send_control(const std::string& action); // pause/resume/stop selected
    void save_camera_snapshot();   // write the latest camera frame to a file

    std::unique_ptr<ForgeCloudAgent> m_agent;

    wxStaticText* m_status_label   = nullptr;
    wxStaticText* m_server_label   = nullptr;
    wxListCtrl*   m_list           = nullptr;
    wxButton*     m_btn_refresh    = nullptr;
    wxButton*     m_btn_configure  = nullptr;
    wxButton*     m_btn_login      = nullptr;
    wxButton*     m_btn_print      = nullptr;
    wxStaticText* m_detail_label   = nullptr;
    wxStaticBitmap* m_camera       = nullptr;
    wxButton*     m_btn_pause       = nullptr;
    wxButton*     m_btn_resume      = nullptr;
    wxButton*     m_btn_stop        = nullptr;
    ForgeControlPanel* m_control    = nullptr;  // native Control widgets (Klipper/Moonraker)
    wxPanel*      m_filament_row    = nullptr;  // filament slots (multi-tool printers)
    wxPanel*      m_slot[4]         = { nullptr, nullptr, nullptr, nullptr }; // color swatch per tool
    wxStaticText* m_slot_lbl[4]     = { nullptr, nullptr, nullptr, nullptr }; // T#/material per tool

    // Print-task card (mirrors Bambu Device's PrintingTaskPanel).
    wxStaticText* m_job_name        = nullptr;  // current sub-task / file name
    wxStaticText* m_stage_label     = nullptr;  // print stage ("Printing"…)
    wxStaticText* m_progress_pct    = nullptr;  // big "NN%"
    ProgressBar*  m_progress_bar    = nullptr;  // styled progress bar
    wxStaticText* m_layer_time      = nullptr;  // "Layer x/y · time left · speed · filament"
    StepIndicator* m_stage_steps    = nullptr;  // generic print-stage flow (Prepare…Done)
    wxPanel*      m_error_banner    = nullptr;  // red HMS/fault banner (hidden when no error)
    wxStaticText* m_error_text      = nullptr;
    wxButton*     m_btn_load        = nullptr;  // filament load
    wxButton*     m_btn_unload      = nullptr;  // filament unload
    wxButton*     m_btn_change      = nullptr;  // filament change (M600)

    wxTimer       m_poll_timer;

    std::string   m_selected_printer_id;
    std::string   m_last_frame;    // most recent camera JPEG (for Snapshot)
    bool          m_corrected_initial = false; // one-time auto-select correction
    std::vector<ForgePrinter> m_printers;
};

}} // namespace Slic3r::GUI

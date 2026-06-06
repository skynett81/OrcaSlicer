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

namespace Slic3r { namespace GUI {

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
    wxPanel*      m_motion_panel    = nullptr;  // jog/extrude/home/tools (Klipper/Moonraker)
    wxPanel*      m_filament_row    = nullptr;  // filament slots (multi-tool printers)
    wxPanel*      m_slot[4]         = { nullptr, nullptr, nullptr, nullptr }; // color swatch per tool
    wxStaticText* m_slot_lbl[4]     = { nullptr, nullptr, nullptr, nullptr }; // T#/material per tool
    wxTimer       m_poll_timer;

    std::string   m_selected_printer_id;
    std::vector<ForgePrinter> m_printers;
};

}} // namespace Slic3r::GUI

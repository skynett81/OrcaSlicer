#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <optional>

namespace Slic3r {

// Single printer entry returned by 3DPrintForge Server.
struct ForgePrinter {
    std::string id;
    std::string name;
    std::string vendor;
    std::string model;
    std::string ip;
    std::string status;
    std::string state;
    std::string current_job;
    int         progress_pct = 0;
    std::string error_message;
};

// Per-toolhead state for multi-extruder printers (e.g. Snapmaker U1).
struct ForgeToolState {
    double      temp     = -1;
    double      target   = -1;
    std::string filament;   // material type, empty if none loaded
    std::string color;      // hex, empty if unknown
};

// Live runtime state for a printer (best-effort, fields are -1 when unknown).
struct ForgeLiveState {
    bool        ok = false;
    int         progress_pct = -1;
    double      nozzle_temp  = -1;
    double      bed_temp     = -1;
    double      chamber_temp = -1;
    std::string state;
    int         active_tool  = -1;          // index of the active toolhead
    std::vector<ForgeToolState> tools;      // per-toolhead (empty = single nozzle)
};

// Authentication / connection state to 3DPrintForge Server.
struct ForgeAuthState {
    bool        signed_in = false;
    std::string server_url;
    std::string session_token;
    std::string username;
    std::string last_error;
};

// Lightweight HTTP client for the 3DPrintForge Server REST API.
// Replaces OrcaCloudServiceAgent — talks to the user's own Forge hub
// instead of a vendor cloud. Uses cpp-httplib for sync calls.
class ForgeCloudAgent {
public:
    ForgeCloudAgent();
    ~ForgeCloudAgent();

    void set_server_url(const std::string& url);
    std::string server_url() const { return m_server_url; }

    bool login(const std::string& username, const std::string& password);
    void logout();
    bool is_signed_in() const { return m_auth.signed_in; }
    const ForgeAuthState& auth_state() const { return m_auth; }

    // Pings /api/auth/status — returns true if our session is still valid.
    bool refresh_session();

    // Fetches the printer roster. Returns empty list on failure;
    // last_error in auth_state holds the reason.
    std::vector<ForgePrinter> list_printers();

    // Starts a print job on the named printer. Returns job id on success.
    std::optional<std::string> start_print(const std::string& printer_id,
                                           const std::string& gcode_path);

    // Fetches the latest camera JPEG frame for a printer (raw bytes), or
    // an empty string on failure. Endpoint: GET /api/printers/{id}/frame.jpeg.
    std::string get_camera_frame(const std::string& printer_id);

    // Sends a print-control action (pause|resume|stop) to a printer.
    // Endpoint: POST /api/printers/{id}/control. Returns true on success.
    bool control_printer(const std::string& printer_id, const std::string& action);

    // Fetches live runtime state (temps/progress) for a printer.
    // Endpoint: GET /api/printers/{id}/state.
    ForgeLiveState get_printer_state(const std::string& printer_id);

    // Motion / tool control (Klipper/Moonraker printers, e.g. Snapmaker U1).
    // All POST /api/printers/{id}/control. Return true on success.
    bool control_home(const std::string& printer_id);
    bool control_move(const std::string& printer_id, const std::string& axis, double dist_mm);
    bool control_extrude(const std::string& printer_id, double amount_mm);
    bool control_tool(const std::string& printer_id, int tool_index);

    // Fan / chamber-light / print-speed (Bambu via MQTT, Klipper/Moonraker via
    // gcode). control_speed takes a percentage (10-300); for Bambu it is mapped
    // to the nearest preset level (1-4). All POST /api/printers/{id}/control.
    bool control_fan(const std::string& printer_id, int percent);
    bool control_light(const std::string& printer_id, bool on);
    bool control_speed(const std::string& printer_id, int percent);

    // Returns time of last successful API call — UI uses this to show
    // freshness in the fleet panel ("synced 12s ago").
    std::chrono::steady_clock::time_point last_synced() const { return m_last_synced; }

private:
    std::string                            m_server_url;
    ForgeAuthState                         m_auth;
    std::chrono::steady_clock::time_point  m_last_synced;
};

} // namespace Slic3r

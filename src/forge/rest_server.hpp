#pragma once

#include <string>

namespace Slic3r {
class PresetBundle;
}

namespace forge_slicer {

// Inject the OrcaSlicer PresetBundle that the REST service should read
// profiles from. Pass nullptr to detach. Safe to call before or after
// start(). In GUI mode this is &wxGetApp().preset_bundle; in headless
// mode the caller must construct a PresetBundle from data_dir() first.
void set_preset_bundle(Slic3r::PresetBundle* bundle);

// Start the embedded HTTP server. Blocks the calling thread — invoke
// from a std::thread when you need the slicer to keep running normally.
// bind defaults to loopback ("127.0.0.1"). token may be empty to disable
// Bearer auth (loopback-only deployments).
void start(int port, const std::string& bind = "127.0.0.1",
           const std::string& token = "");

} // namespace forge_slicer

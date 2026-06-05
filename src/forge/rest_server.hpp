#pragma once

#include <string>

namespace forge_slicer {

// Start the embedded HTTP server. Blocks the calling thread — invoke
// from a std::thread when you need the slicer to keep running normally.
// bind defaults to loopback ("127.0.0.1"). token may be empty to disable
// Bearer auth (loopback-only deployments).
void start(int port, const std::string& bind = "127.0.0.1",
           const std::string& token = "");

} // namespace forge_slicer

#pragma once

namespace cc::gl {

// Installs glDebugMessageCallback when KHR_debug is available; no-op otherwise.
// High-severity messages assert in debug builds.
void enableDebugOutput();

} // namespace cc::gl

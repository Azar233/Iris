#pragma once

// Enables KHR_debug diagnostics for Debug builds when the driver supports it.
void initializeOpenGlDebugOutput();
// Opt-in verification hook. Requires RenderDoc to have injected this process.
bool triggerRenderDocFrameCapture();

#pragma once

#include <string>

namespace ever::browser {

bool InitializeNativeOverlayRenderer();
void TickNativeOverlayRenderer();
void ShutdownNativeOverlayRenderer();
void OnPresentNativeOverlay(void* swap_chain_ptr);
bool IsNativeOverlayRendererActive();
bool IsNativeOverlayUsingDxgiHook();
void EnableCEFInteractions(bool enabled);
bool AreCEFInteractionsEnabled();
void EnableCEFKeyboardInteractions(bool enabled);
bool AreCEFKeyboardInteractionsEnabled();
bool CreateNativeOverlayFrameFromUrl(const std::string& frame_name, const std::string& frame_url);
bool CreateNativeOverlayFrameFromHtml(const std::string& frame_name, const std::string& html_document);
bool CreateNativeOverlayFrameFromPath(const std::string& frame_name,
                                      const std::string& overlay_path,
                                      const std::string& entry_document_relative_path = "index.html");
bool DestroyNativeOverlayFrame(const std::string& frame_name);
bool PostNativeOverlayFrameMessage(const std::string& frame_name, const std::string& json_payload);
bool ExecuteNativeOverlayRootScript(const std::string& javascript);
bool SendCefMessage(const std::string& json_payload);
bool PollCefMessage(std::string& out_json_payload);

}
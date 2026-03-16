#include "ever/browser/native_overlay_renderer_internal.h"

#include <string>

#include <windows.h>

namespace ever::browser::native_overlay_internal {

namespace {

std::string LoadEmbeddedTextResource(const wchar_t* resource_name, const wchar_t* resource_type) {
    if (resource_name == nullptr || resource_type == nullptr) {
        return std::string();
    }

    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&LoadEmbeddedTextResource),
            &module)) {
        return std::string();
    }

    HRSRC resource = FindResourceW(module, resource_name, resource_type);
    if (resource == nullptr) {
        return std::string();
    }

    HGLOBAL loaded = LoadResource(module, resource);
    if (loaded == nullptr) {
        return std::string();
    }

    const DWORD size = SizeofResource(module, resource);
    if (size == 0) {
        return std::string();
    }

    const void* data = LockResource(loaded);
    if (data == nullptr) {
        return std::string();
    }

    return std::string(reinterpret_cast<const char*>(data), static_cast<size_t>(size));
}

} // namespace

std::string BuildRootFrameManagerHtml() {
  std::string html = LoadEmbeddedTextResource(L"EVER2_NATIVE_OVERLAY_ROOT_HTML", RT_HTML);
    if (!html.empty()) {
        Log(L"[EVER2] Loaded root frame manager HTML from embedded resource.");
        return html;
    }

    Log(L"[EVER2] Failed to load embedded root frame manager HTML; using fallback inline document.");
    return "<!doctype html><html><body style='margin:0;background:transparent;overflow:hidden'><script>"
            "const __everFrames=Object.create(null);"
            "const __everId=(v)=>String(v??'').trim();"
            "const __everEnsure=(id)=>{const n=__everId(id);if(!n)return null;if(__everFrames[n])return __everFrames[n];const f=document.createElement('iframe');f.name=n;f.style='position:absolute;inset:0;border:0;width:100%;height:100%';document.body.appendChild(f);__everFrames[n]=f;return f;};"
            "window.everCreateFrameFromUrl=(n,u)=>{const f=__everEnsure(n);if(!f)return false;f.src=String(u??'about:blank');return true;};"
            "window.everCreateFrameFromHtml=(n,h)=>{const f=__everEnsure(n);if(!f)return false;f.srcdoc=String(h??'');return true;};"
            "window.everDestroyFrame=(n)=>{const id=__everId(n);const f=__everFrames[id];if(!f)return false;delete __everFrames[id];if(f.parentNode)f.parentNode.removeChild(f);return true;};"
            "window.everPostFrameMessage=(n,p)=>{const id=__everId(n);const f=__everFrames[id];if(!f||!f.contentWindow)return false;let m=p;if(typeof p==='string'){try{m=JSON.parse(p);}catch(_){m=p;}}f.contentWindow.postMessage(m,'*');return true;};"
            "window.everListFrames=()=>Object.keys(__everFrames);"
            "window.everBroadcastCefMessage=(p)=>{window.dispatchEvent(new CustomEvent('ever:cef:message',{detail:p}));for(const id of Object.keys(__everFrames)){window.everPostFrameMessage(id,p);}return true;};"
            "const __everSendViaConsoleBridge=(p)=>{const payload=typeof p==='string'?p:JSON.stringify(p??null);console.log('__EVER2_CEF_BRIDGE__:'+payload);return true;};"
            "window.everSendCefMessageToHost=(p)=>{const payload=typeof p==='string'?p:JSON.stringify(p??null);if(typeof window.everNativeSendCefMessage==='function'){try{return !!window.everNativeSendCefMessage(payload);}catch(_){return __everSendViaConsoleBridge(payload);}}return __everSendViaConsoleBridge(payload);};"
            "window.addEventListener('message',(event)=>{if(!event||event.source===window||event.data==null)return;const d=event.data;if(d&&typeof d==='object'&&Object.prototype.hasOwnProperty.call(d,'__everSendCefMessage')){window.everSendCefMessageToHost(d.__everSendCefMessage);return;}if(typeof d==='string'){window.everSendCefMessageToHost(d);return;}if(typeof d==='object'){window.everSendCefMessageToHost(d);}});"
           "</script></body></html>";
}

// Temp used for testing and debugging
std::string BuildLegacyGreetingHtmlDocument() {
    return R"HTML(
<html>
<body style='margin:0;overflow:hidden;background:transparent;'>
  <div style='position:absolute;left:40px;top:32px;padding:14px 18px;border-radius:10px;background:rgba(18,24,33,0.72);color:rgba(248,251,255,1.0);font:700 30px Segoe UI;'>
    Heard you like running HTML code in GTA?
    <div id='ever2-click-status' style='margin-top:14px;font:600 16px Segoe UI;color:#DCE8FF;'>
      Status: waiting for click
    </div>
    <button id='ever2-test-button' style='margin-top:10px;padding:10px 16px;font:700 18px Segoe UI;border:0;border-radius:8px;background:#31C48D;color:#0B1A13;cursor:pointer;'>
      Helloo
    </button>
    <button id='ever2-bridge-button' style='margin-top:10px;margin-left:8px;padding:10px 16px;font:700 18px Segoe UI;border:0;border-radius:8px;background:#5B8DEF;color:#081126;cursor:pointer;'>
      Send To C++ Log
    </button>
    <script>
      (function(){
        const btn=document.getElementById('ever2-test-button');
        const bridgeBtn=document.getElementById('ever2-bridge-button');
        const status=document.getElementById('ever2-click-status');
        if(!btn||!bridgeBtn||!status){
          console.log('[EVER2-TEST] setup failed');
          return;
        }

        let clicks=0;
        btn.addEventListener('click',()=>{
          clicks++;
          const t=new Date().toLocaleTimeString();
          status.textContent='Status: click received #' + clicks + ' at ' + t;
          btn.textContent='Clicked ' + clicks;
          btn.style.background = clicks % 2 === 0 ? '#31C48D' : '#F5A623';
          console.log('[EVER2-TEST] button click', clicks, t);
        });

        btn.addEventListener('pointerdown',()=>console.log('[EVER2-TEST] pointerdown'));
        btn.addEventListener('pointerup',()=>console.log('[EVER2-TEST] pointerup'));

        bridgeBtn.addEventListener('click',()=>{
          const payload='[EVER2-TEST] JS button -> C++ bridge at '+new Date().toISOString();
          try {
            let sent=false;

            if(window.parent&&typeof window.parent.everSendCefMessageToHost==='function'){
              sent=!!window.parent.everSendCefMessageToHost(payload);
              console.log('[EVER2-TEST] bridge direct parent call result', sent, payload);
            }

            if(!sent){
              window.parent.postMessage({__everSendCefMessage:payload},'*');
              console.log('__EVER2_CEF_BRIDGE__:'+payload);
              console.log('[EVER2-TEST] bridge fallback postMessage sent', payload);
              sent=true;
            }

            status.textContent=sent?'Status: sent bridge message to C++ log':'Status: bridge send failed';
          } catch(err) {
            status.textContent='Status: bridge send failed';
            console.log('[EVER2-TEST] bridge message post failed', err);
          }
        });

        console.log('[EVER2-TEST] click test ready');
      })();
    </script>
  </div>
</body>
</html>
)HTML";
}

}

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <lwip/sockets.h>
#include <nvs.h>
#include <soc/soc_caps.h>

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>

#include "diagnostics_logic.h"

#ifndef DIAG_SDA_PIN
#error "DIAG_SDA_PIN is required"
#endif
#ifndef DIAG_SCL_PIN
#error "DIAG_SCL_PIN is required"
#endif
#ifndef DIAG_SPI_SCK_PIN
#error "DIAG_SPI_SCK_PIN is required"
#endif
#ifndef DIAG_SPI_MISO_PIN
#error "DIAG_SPI_MISO_PIN is required"
#endif
#ifndef DIAG_SPI_MOSI_PIN
#error "DIAG_SPI_MOSI_PIN is required"
#endif
#ifndef DIAG_SPI_SS_PIN
#error "DIAG_SPI_SS_PIN is required"
#endif
#ifndef SETUP_BUTTON_PIN
#error "SETUP_BUTTON_PIN is required"
#endif

namespace {
constexpr uint32_t kConfigMagic = 0x44494147U;
constexpr uint16_t kConfigVersion = 1U;
constexpr uint32_t kConnectTimeoutMs = 30000U;
constexpr uint32_t kRequestTimeoutMs = 750U;
constexpr uint32_t kResponseDeadlineMs = 1000U;
constexpr uint32_t kI2cScanCooldownMs = 10000U;
constexpr uint32_t kRecoveryWindowMs = 5000U;
constexpr uint32_t kRecoveryHoldMs = 1500U;
constexpr uint8_t kMaximumRequestsPerSecond = 12U;
constexpr uint32_t kProvisioningTimeoutMs = 10U * 60U * 1000U;
constexpr size_t kMaximumRequestBytes = 2048U;
constexpr size_t kMaximumHeaderBytes = 1024U;
constexpr size_t kMaximumBodyBytes = 512U;
constexpr size_t kSetupPasswordCharacters = 8U;
constexpr char kSetupPasswordAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static_assert(sizeof(kSetupPasswordAlphabet) - 1U == 32U, "setup alphabet must contain 32 symbols");

struct StoredConfig {
    uint32_t magic;
    uint16_t version;
    char ssid[33];
    char password[64];
    uint32_t crc32;
};
static_assert(sizeof(StoredConfig) < 128U, "diagnostics configuration should stay small");

struct I2cScan {
    uint8_t addresses[32]{};
    uint8_t count = 0U;
    uint8_t total = 0U;
    bool truncated = false;
    bool started = false;
    uint64_t completed_uptime_ms = 0U;
};

DNSServer dns;
WiFiServer server(80);
StoredConfig active_config{};
I2cScan i2c_scan{};
bool provisioning = false;
bool restart_requested = false;
uint32_t provisioning_started_ms = 0U;
char csrf_token[17]{};
char dashboard_token[17]{};
uint32_t request_count = 0U;
uint32_t request_window_started_ms = 0U;
uint8_t requests_in_window = 0U;
uint32_t last_scan_started_ms = 0U;
bool has_scan_started = false;
uint32_t uptime_previous_ms = 0U;
uint64_t uptime_total_ms = 0U;
bool uptime_started = false;

const char kSetupPageStart[] PROGMEM = R"HTML(<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>ESP Diagnostics Setup</title><style>:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#0b0e10;color:#e9f0ec;font:16px/1.5 ui-sans-serif,system-ui,sans-serif}main{max-width:560px;margin:10vh auto;padding:24px}p:first-child{color:#91a098;font:700 12px ui-monospace,monospace;letter-spacing:.12em}h1{font-size:clamp(2rem,8vw,4rem);line-height:.95;letter-spacing:-.06em;margin:.4em 0}.panel{border:1px solid #2a3430;background:#121715;padding:20px}label{display:grid;gap:6px;margin:16px 0;color:#b4c0ba}input,button{width:100%;min-height:48px;border-radius:3px;font:inherit}input{border:1px solid #39453f;background:#090c0a;color:#fff;padding:0 12px}button{border:0;background:#b8f36b;color:#152007;font-weight:800;cursor:pointer}.note{color:#91a098;font-size:14px}.message{padding:12px;border:1px solid #b8f36b;color:#dfffb5}</style></head><body><main><p>LOCAL DEVICE SETUP</p><h1>ESP diagnostics</h1>)HTML";

const char kDashboard[] PROGMEM = R"HTML(<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>ESP Diagnostics</title><style>
:root{color-scheme:dark;--bg:#090c0b;--surface:#111613;--line:#29332e;--ink:#edf5f0;--muted:#91a098;--accent:#b8f36b;--warn:#ffca6a;--bad:#ff786f}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 ui-sans-serif,system-ui,sans-serif}button{font:inherit}header{position:sticky;top:0;z-index:5;display:flex;align-items:center;gap:16px;padding:14px clamp(16px,3vw,38px);background:#090c0bf2;border-bottom:1px solid var(--line)}.brand{font-size:14px;margin:0;font-weight:850;letter-spacing:-.025em}.live{display:flex;align-items:center;gap:7px;color:var(--muted);font:12px ui-monospace,monospace}.dot{width:7px;height:7px;border-radius:50%;background:var(--accent);box-shadow:0 0 0 4px #b8f36b18}.live.offline .dot{background:var(--bad);box-shadow:0 0 0 4px #ff786f18}.live.paused .dot{background:var(--warn);box-shadow:0 0 0 4px #ffca6a18}.actions,.ranges{display:flex;gap:8px;flex-wrap:wrap}.actions{margin-left:auto}.control{min-height:44px;border:1px solid var(--line);border-radius:3px;background:var(--surface);color:var(--ink);padding:0 12px;cursor:pointer}.control:hover{border-color:var(--accent)}.control:focus-visible{outline:2px solid var(--accent);outline-offset:2px}.control[aria-pressed=true],.control[aria-checked=true]{background:var(--accent);color:#152007;border-color:var(--accent)}.sr-only{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap;border:0}main{max-width:1500px;margin:auto;padding:24px clamp(16px,3vw,38px) 60px}.overview{display:grid;grid-template-columns:repeat(6,minmax(120px,1fr));border:1px solid var(--line);background:var(--surface)}.metric{padding:16px;border-right:1px solid var(--line)}.metric:last-child{border:0}.label{color:var(--muted);font:11px ui-monospace,monospace;text-transform:uppercase;letter-spacing:.08em}.value{font:700 clamp(20px,2.5vw,31px)/1.2 ui-monospace,monospace;letter-spacing:-.04em;margin-top:8px}.section-head{display:flex;align-items:end;justify-content:space-between;margin:30px 0 10px}.section-head h2{font-size:18px;margin:0;letter-spacing:-.02em}.section-head p{margin:0;color:var(--muted)}.charts{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.chart{border:1px solid var(--line);background:var(--surface);padding:14px;min-width:0}.chart-head{display:flex;justify-content:space-between}.chart strong{font:650 13px ui-monospace,monospace}.chart span{color:var(--muted);font:12px ui-monospace,monospace}canvas{display:block;width:100%;height:170px;margin-top:8px}.details{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.panel{border:1px solid var(--line);background:var(--surface);padding:18px}.panel h3{margin:0 0 14px;font-size:14px}.rows{display:grid;grid-template-columns:minmax(130px,.7fr) 1fr;margin:0}.rows dt,.rows dd{padding:8px 0;margin:0;border-top:1px solid #202824}.rows dt{color:var(--muted)}.rows dd{font:13px ui-monospace,monospace;overflow-wrap:anywhere}.addresses{display:flex;gap:7px;flex-wrap:wrap}.address{border:1px solid var(--line);padding:7px 9px;font:12px ui-monospace,monospace}.empty{color:var(--muted)}.error{color:var(--bad)}footer{color:var(--muted);padding-top:30px;font-size:12px}@media(max-width:900px){.overview{grid-template-columns:repeat(3,1fr)}.metric:nth-child(3){border-right:0}.metric:nth-child(-n+3){border-bottom:1px solid var(--line)}.charts{grid-template-columns:1fr}.details{grid-template-columns:1fr}}@media(max-width:520px){header{align-items:flex-start;flex-wrap:wrap}.actions{width:100%;margin:0}.overview{grid-template-columns:repeat(2,1fr)}.metric:nth-child(n){border-right:1px solid var(--line);border-bottom:1px solid var(--line)}.metric:nth-child(2n){border-right:0}.metric:nth-last-child(-n+2){border-bottom:0}.section-head{align-items:start;gap:8px;flex-direction:column}}@media(prefers-reduced-motion:reduce){*{scroll-behavior:auto!important}}
</style></head><body><header><h1 class=brand>ESP / DIAGNOSTICS</h1><div id=connection class=live role=status aria-live=polite><span class=dot></span><span id=status>connecting</span></div><div class=actions><button class=control id=pause aria-pressed=false>Pause</button><div class=ranges role=radiogroup aria-label="Chart time range"><button class=control role=radio data-range=60 aria-checked=false>1m</button><button class=control role=radio data-range=300 aria-checked=true>5m</button><button class=control role=radio data-range=900 aria-checked=false>15m</button></div><button class=control id=rescan>Rescan I²C</button></div></header><main><section class=overview aria-label="Live overview"><div class=metric><div class=label>Free heap</div><div class=value id=heap>—</div></div><div class=metric><div class=label>Minimum heap</div><div class=value id=minHeap>—</div></div><div class=metric><div class=label>Wi-Fi signal</div><div class=value id=rssi>—</div></div><div class=metric><div class=label>Chip temperature</div><div class=value id=temp>—</div></div><div class=metric><div class=label>Uptime</div><div class=value id=uptime>—</div></div><div class=metric><div class=label>I²C devices</div><div class=value id=i2cCount>—</div></div></section><div class=section-head><h2>Live telemetry</h2><p>Polling target: once per second · gaps are preserved</p></div><section class=charts><figure class=chart><figcaption class=chart-head><strong>Heap availability</strong><span id=heapNow>—</span></figcaption><canvas id=heapChart role=img aria-label="Free heap history" aria-describedby=heapChartSummary></canvas><p class=sr-only id=heapChartSummary>No history yet</p></figure><figure class=chart><figcaption class=chart-head><strong>Wi-Fi RSSI</strong><span id=rssiNow>—</span></figcaption><canvas id=rssiChart role=img aria-label="Wi-Fi signal history" aria-describedby=rssiChartSummary></canvas><p class=sr-only id=rssiChartSummary>No history yet</p></figure><figure class=chart><figcaption class=chart-head><strong>Internal temperature</strong><span id=tempNow>—</span></figcaption><canvas id=tempChart role=img aria-label="Chip temperature history" aria-describedby=tempChartSummary></canvas><p class=sr-only id=tempChartSummary>No history yet</p></figure></section><div class=section-head><h2>Device inspection</h2><p id=updated role=status aria-live=polite>Waiting for first sample</p></div><p class=sr-only id=scanStatus role=status aria-live=polite></p><section class=details><article class=panel><h3>System</h3><dl class=rows id=systemRows></dl></article><article class=panel><h3>Network</h3><dl class=rows id=networkRows></dl></article><article class=panel><h3>I²C bus</h3><dl class=rows id=i2cRows></dl><div class=addresses id=addresses></div></article><article class=panel><h3>SPI & interfaces</h3><dl class=rows id=interfaceRows></dl></article></section><footer>Read-only diagnostics except the explicit I²C rescan. SPI devices cannot be safely enumerated without chip-select and protocol knowledge.</footer></main><script>
const TOKEN='__TOKEN__';const samples=[];let paused=false,scanning=false,range=300,pollController=null,pollSequence=0,pollTimer=0;const $=id=>document.getElementById(id);const fmtBytes=n=>Number.isFinite(n)?(n>=1048576?(n/1048576).toFixed(1)+' MB':n>=1024?Math.round(n/1024)+' KB':n+' B'):'Unavailable';const fmtTemp=n=>Number.isFinite(n)?n.toFixed(1)+' °C':'Unavailable';const fmtUp=ms=>{if(!Number.isFinite(ms)||ms<0)return'Unavailable';let s=Math.floor(ms/1000),d=Math.floor(s/86400);s%=86400;const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m'};function rows(id,data){$(id).replaceChildren(...Object.entries(data).flatMap(([k,v])=>{const dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=k;dd.textContent=v??'—';return[dt,dd]}))}function render(d){$('heap').textContent=fmtBytes(d.memory.free_heap);$('minHeap').textContent=fmtBytes(d.memory.minimum_free_heap);$('rssi').textContent=d.wifi.rssi_dbm+' dBm';$('temp').textContent=fmtTemp(d.system.temperature_c);$('uptime').textContent=fmtUp(d.system.uptime_ms);$('i2cCount').textContent=d.i2c.total;$('heapNow').textContent=fmtBytes(d.memory.free_heap);$('rssiNow').textContent=d.wifi.rssi_dbm+' dBm';$('tempNow').textContent=fmtTemp(d.system.temperature_c);rows('systemRows',{'Model':d.system.model,'Revision':d.system.revision,'CPU':d.system.cores+' cores · '+d.system.cpu_mhz+' MHz','Reset reason':d.system.reset_reason,'Wake cause':d.system.wake_cause,'Flash':fmtBytes(d.flash.size)+' · '+d.flash.speed_mhz+' MHz','Sketch partition':fmtBytes(d.flash.sketch_size)+' used / '+fmtBytes(d.flash.sketch_partition_size),'Next OTA slot':fmtBytes(d.flash.next_ota_partition_size),'PSRAM':d.memory.psram_size?fmtBytes(d.memory.free_psram)+' free / '+fmtBytes(d.memory.psram_size):'Not detected','Largest block':fmtBytes(d.memory.largest_block),'SDK':d.system.sdk});rows('networkRows',{'SSID':d.wifi.ssid,'IPv4':d.wifi.ip,'Gateway':d.wifi.gateway,'Subnet':d.wifi.subnet,'DNS':d.wifi.dns,'MAC':d.wifi.mac,'BSSID':d.wifi.bssid,'Channel':d.wifi.channel,'Requests served':d.network.requests});rows('i2cRows',{'Pins':'SDA GPIO'+d.i2c.sda+' · SCL GPIO'+d.i2c.scl,'Status':d.i2c.started?'Scan complete':'Bus unavailable','Last scan':d.i2c.started?fmtUp(Math.max(0,d.system.uptime_ms-d.i2c.completed_uptime_ms))+' ago':'Unavailable','Visible / total':d.i2c.addresses.length+' / '+d.i2c.total});const box=$('addresses');box.replaceChildren();if(!d.i2c.addresses.length){const e=document.createElement('span');e.className='empty';e.textContent='No responding addresses';box.append(e)}else d.i2c.addresses.forEach(x=>{const e=document.createElement('span');e.className='address';e.textContent=x.hex+(x.name?' · '+x.name:'');box.append(e)});rows('interfaceRows',{'SPI pins':'SCK GPIO'+d.spi.sck+' · MISO GPIO'+d.spi.miso+' · MOSI GPIO'+d.spi.mosi+' · SS GPIO'+d.spi.ss,'SPI discovery':'Not possible generically','GPIO count':d.interfaces.gpio_count,'Hardware UARTs':d.interfaces.uart_count,'Hardware SPI buses':d.interfaces.spi_count,'Hardware I²C buses':d.interfaces.i2c_count,'Bluetooth':d.interfaces.bluetooth,'Wi-Fi radio':d.interfaces.wifi});$('updated').textContent='Updated '+new Date().toLocaleTimeString();drawAll()}function chart(canvasId,key,color){const c=$(canvasId),rect=c.getBoundingClientRect(),ratio=devicePixelRatio||1;c.width=Math.max(1,Math.floor(rect.width*ratio));c.height=Math.max(1,Math.floor(rect.height*ratio));const x=c.getContext('2d');x.scale(ratio,ratio);const w=rect.width,h=rect.height,p=18,now=Date.now(),data=samples.filter(s=>now-s.t<=range*1000&&Number.isFinite(s[key]));x.clearRect(0,0,w,h);x.strokeStyle='#26312b';x.lineWidth=1;for(let i=0;i<4;i++){const y=p+(h-p*2)*i/3;x.beginPath();x.moveTo(p,y);x.lineTo(w-p,y);x.stroke()}const summary=$(canvasId+'Summary');if(!data.length){summary.textContent='No history in the selected range';return}const values=data.map(s=>s[key]),rawMin=Math.min(...values),rawMax=Math.max(...values),delta=values.at(-1)-values[0],format=key==='heap'?fmtBytes:key==='rssi'?(v=>v.toFixed(0)+' dBm'):fmtTemp;summary.textContent=data.length+' samples. Minimum '+format(rawMin)+', maximum '+format(rawMax)+', change '+(delta>=0?'+':'')+format(delta)+'.';if(data.length<2)return;let min=rawMin,max=rawMax;if(max===min){max+=1;min-=1}const span=max-min;x.strokeStyle=color;x.lineWidth=2;x.beginPath();data.forEach((s,i)=>{const px=p+(w-p*2)*(s.t-(now-range*1000))/(range*1000),py=h-p-(h-p*2)*(s[key]-min)/span;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}function drawAll(){requestAnimationFrame(()=>{chart('heapChart','heap','#b8f36b');chart('rssiChart','rssi','#69d6c4');chart('tempChart','temp','#ffca6a')})}function validPayload(d){return d&&d.system&&d.memory&&d.flash&&d.wifi&&d.network&&d.i2c&&d.spi&&d.interfaces&&Number.isFinite(d.system.uptime_ms)&&Number.isFinite(d.memory.free_heap)&&Number.isFinite(d.memory.minimum_free_heap)&&Number.isFinite(d.wifi.rssi_dbm)&&Array.isArray(d.i2c.addresses)}function setConnection(state,label){$('connection').className='live '+state;$('status').textContent=label}function schedulePoll(delay=1000){clearTimeout(pollTimer);if(!paused&&!scanning&&!document.hidden)pollTimer=setTimeout(poll,delay)}async function poll(){if(paused||document.hidden||pollController)return;const sequence=++pollSequence;pollController=new AbortController();const controller=pollController;try{const r=await fetch('api/diagnostics',{cache:'no-store',signal:controller.signal});if(!r.ok)throw Error(r.status);const d=await r.json();if(paused||sequence!==pollSequence)return;if(!validPayload(d))throw Error('payload');samples.push({t:Date.now(),heap:d.memory.free_heap,rssi:d.wifi.rssi_dbm,temp:d.system.temperature_c});while(samples.length>900)samples.shift();render(d);setConnection('','live')}catch(e){if(e.name!=='AbortError'&&!paused)setConnection('offline','offline')}finally{if(pollController===controller)pollController=null;schedulePoll()}}$('pause').onclick=()=>{paused=!paused;$('pause').setAttribute('aria-pressed',paused);$('pause').textContent=paused?'Resume':'Pause';if(paused){++pollSequence;if(pollController)pollController.abort();pollController=null;clearTimeout(pollTimer);setConnection('paused','paused')}else{setConnection('','connecting');schedulePoll(0)}};const rangeButtons=[...document.querySelectorAll('[data-range]')];function chooseRange(b){range=+b.dataset.range;rangeButtons.forEach(x=>{const selected=x===b;x.setAttribute('aria-checked',selected);x.tabIndex=selected?0:-1});drawAll()}rangeButtons.forEach((b,index)=>{b.onclick=()=>chooseRange(b);b.onkeydown=e=>{if(e.key==='ArrowRight'||e.key==='ArrowLeft'){e.preventDefault();const step=e.key==='ArrowRight'?1:-1,next=rangeButtons[(index+step+rangeButtons.length)%rangeButtons.length];chooseRange(next);next.focus()}}});$('rescan').onclick=async()=>{scanning=true;++pollSequence;clearTimeout(pollTimer);if(pollController)pollController.abort();pollController=null;const b=$('rescan');b.disabled=true;b.setAttribute('aria-busy','true');b.textContent='Scanning…';$('scanStatus').textContent='I2C scan started';try{const r=await fetch('api/i2c/scan',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+TOKEN});if(!r.ok)throw Error(r.status);const d=await r.json();if(!validPayload(d))throw Error('payload');render(d);$('scanStatus').textContent='I2C scan complete: '+d.i2c.total+' responding addresses'}catch(e){$('scanStatus').textContent=e.message==='429'?'I2C scan is cooling down; wait ten seconds':'I2C scan failed'}finally{scanning=false;b.disabled=false;b.removeAttribute('aria-busy');b.textContent='Rescan I²C';schedulePoll()}};let resizeTimer=0;addEventListener('resize',()=>{clearTimeout(resizeTimer);resizeTimer=setTimeout(drawAll,100)});document.addEventListener('visibilitychange',()=>{if(document.hidden){clearTimeout(pollTimer);if(pollController)pollController.abort();pollController=null}else if(!paused)schedulePoll(0)});chooseRange(rangeButtons[1]);schedulePoll(0);</script></body></html>)HTML";

uint32_t configCrc(const StoredConfig& config) {
    return diagnosticCrc32(reinterpret_cast<const uint8_t*>(&config), offsetof(StoredConfig, crc32));
}

bool validConfig(const StoredConfig& config) {
    return config.magic == kConfigMagic && config.version == kConfigVersion && config.crc32 == configCrc(config) &&
           std::memchr(config.ssid, '\0', sizeof(config.ssid)) && std::memchr(config.password, '\0', sizeof(config.password)) &&
           isValidDiagnosticSsid(config.ssid) && isValidDiagnosticPassword(config.password);
}

bool loadConfig(const char* key, StoredConfig& config) {
    Preferences preferences;
    if (!preferences.begin("diagnostics", true)) return false;
    const size_t size = preferences.getBytesLength(key);
    const size_t read = size == sizeof(config) ? preferences.getBytes(key, &config, sizeof(config)) : 0U;
    preferences.end();
    return read == sizeof(config) && validConfig(config);
}

bool storeConfig(const char* key, StoredConfig config) {
    config.crc32 = configCrc(config);
    Preferences preferences;
    if (!preferences.begin("diagnostics", false)) return false;
    const bool written = preferences.putBytes(key, &config, sizeof(config)) == sizeof(config);
    preferences.end();
    if (!written) return false;
    StoredConfig verified{};
    return loadConfig(key, verified) && std::memcmp(&verified, &config, sizeof(config)) == 0;
}

bool removeConfig(const char* key) {
    nvs_handle_t handle = 0;
    if (nvs_open("diagnostics", NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t erased = nvs_erase_key(handle, key);
    bool ok = erased == ESP_ERR_NVS_NOT_FOUND || (erased == ESP_OK && nvs_commit(handle) == ESP_OK);
    size_t length = 0U;
    if (ok) ok = nvs_get_blob(handle, key, nullptr, &length) == ESP_ERR_NVS_NOT_FOUND;
    nvs_close(handle);
    return ok;
}

void randomHex(char* output, size_t bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0U; i < bytes; ++i) {
        const uint8_t value = static_cast<uint8_t>(esp_random());
        output[i * 2U] = hex[value >> 4U];
        output[i * 2U + 1U] = hex[value & 0x0fU];
    }
    output[bytes * 2U] = '\0';
}

template <size_t N> void makeReadableSetupPassword(char (&output)[N]) {
    static_assert(N >= kSetupPasswordCharacters + 1U, "setup password buffer too small");
    for (size_t i = 0U; i < kSetupPasswordCharacters; ++i) output[i] = kSetupPasswordAlphabet[esp_random() & 31U];
    output[kSetupPasswordCharacters] = '\0';
}

uint64_t diagnosticUptimeMs() {
    const uint32_t current = millis();
    if (!uptime_started) {
        uptime_started = true;
        uptime_previous_ms = current;
        uptime_total_ms = current;
    } else {
        uptime_total_ms = extendDiagnosticMillis(uptime_total_ms, uptime_previous_ms, current);
        uptime_previous_ms = current;
    }
    return uptime_total_ms;
}

I2cScan scanI2c() {
    I2cScan result;
    last_scan_started_ms = millis();
    has_scan_started = true;
    result.started = Wire.begin(DIAG_SDA_PIN, DIAG_SCL_PIN, 100000U);
    if (!result.started) return result;
    Wire.setTimeOut(10U);
    for (uint8_t address = 1U; address < 127U; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() != 0U) continue;
        if (result.total < 0xffU) ++result.total;
        if (result.count < sizeof(result.addresses)) result.addresses[result.count++] = address;
        else result.truncated = true;
    }
    Wire.end();
    result.completed_uptime_ms = diagnosticUptimeMs();
    return result;
}

const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "Power on"; case ESP_RST_EXT: return "External reset";
        case ESP_RST_SW: return "Software reset"; case ESP_RST_PANIC: return "Panic";
        case ESP_RST_INT_WDT: return "Interrupt watchdog"; case ESP_RST_TASK_WDT: return "Task watchdog";
        case ESP_RST_WDT: return "Other watchdog"; case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
        case ESP_RST_BROWNOUT: return "Brownout"; case ESP_RST_SDIO: return "SDIO";
        default: return "Unknown";
    }
}

const char* wakeCauseName(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT0: return "External 0"; case ESP_SLEEP_WAKEUP_EXT1: return "External 1";
        case ESP_SLEEP_WAKEUP_TIMER: return "Timer"; case ESP_SLEEP_WAKEUP_TOUCHPAD: return "Touch";
        case ESP_SLEEP_WAKEUP_ULP: return "ULP"; case ESP_SLEEP_WAKEUP_GPIO: return "GPIO";
        case ESP_SLEEP_WAKEUP_UART: return "UART"; default: return "Not from sleep";
    }
}

bool connectWifi(const StoredConfig& config) {
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(config.ssid, config.password);
    const uint32_t started = millis();
    while (static_cast<uint32_t>(millis() - started) < kConnectTimeoutMs) {
        if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0U, 0U, 0U, 0U)) return true;
        delay(100U);
    }
    WiFi.disconnect(true, false);
    return false;
}

bool writeAllBounded(WiFiClient& client, const uint8_t* data, size_t length) {
    const uint32_t started = millis();
    size_t sent = 0U;
    while (sent < length) {
        const uint32_t elapsed = static_cast<uint32_t>(millis() - started);
        if (elapsed >= kResponseDeadlineMs || client.fd() < 0) return false;
        const uint32_t remaining = kResponseDeadlineMs - elapsed;
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(client.fd(), &writable);
        timeval timeout{static_cast<time_t>(remaining / 1000U), static_cast<suseconds_t>((remaining % 1000U) * 1000U)};
        const int ready = select(client.fd() + 1, nullptr, &writable, nullptr, &timeout);
        if (ready == 0) return false;
        if (ready < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        const int result = send(client.fd(), data + sent, length - sent, MSG_DONTWAIT);
        if (result > 0) sent += static_cast<size_t>(result);
        else if (result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        else return false;
    }
    return true;
}

bool sendText(WiFiClient& client, int status, const char* reason, const char* type, const String& body) {
    String response;
    if (!response.reserve(body.length() + 512U)) return false;
    response += "HTTP/1.1 "; response += status; response += ' '; response += reason;
    response += "\r\nContent-Type: "; response += type;
    response += "\r\nContent-Length: "; response += body.length();
    response += "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'\r\nReferrer-Policy: no-referrer\r\n\r\n";
    response += body;
    return writeAllBounded(client, reinterpret_cast<const uint8_t*>(response.c_str()), response.length());
}

String setupPage(const String& message = {}) {
    String page(FPSTR(kSetupPageStart));
    if (!message.isEmpty()) page += "<p class=message>" + message + "</p>";
    page += F("<div class=panel><form method=post action=/save><input type=hidden name=csrf value='");
    page += csrf_token;
    page += F("'><label>Wi-Fi name<input name=ssid maxlength=32 required autocomplete=off></label><label>Wi-Fi password<input name=password type=password maxlength=63 autocomplete=new-password></label><button type=submit>Connect and open diagnostics</button></form><p class=note>Credentials stay in ESP32 NVS. Setup closes after ten minutes. To change networks later, press RESET normally and then hold BOOT during the five-second recovery window.</p></div></main></body></html>");
    return page;
}

struct HttpRequest {
    char method[8]{};
    char path[64]{};
    char data[kMaximumRequestBytes + 1U]{};
    size_t used = 0U;
    size_t header_end = 0U;
    size_t content_length = 0U;
};

bool readRawRequest(WiFiClient& client, HttpRequest& request) {
    const uint32_t started = millis();
    while (client.connected() && static_cast<uint32_t>(millis() - started) < kRequestTimeoutMs) {
        while (client.available()) {
            if (request.used >= kMaximumRequestBytes) return false;
            request.data[request.used++] = static_cast<char>(client.read());
            request.data[request.used] = '\0';
            if (request.header_end == 0U && request.used >= 4U &&
                std::memcmp(request.data + request.used - 4U, "\r\n\r\n", 4U) == 0) {
                request.header_end = request.used;
                if (request.header_end > kMaximumHeaderBytes) return false;
                bool has_content_length = false;
                const String local_ip = WiFi.localIP().toString();
                const bool parsed = provisioning
                    ? parseBoundedHttpRequest(request.data, request.header_end, kMaximumBodyBytes,
                                              request.method, sizeof(request.method), request.path, sizeof(request.path),
                                              request.content_length, has_content_length)
                    : parseBoundedLanHttpRequest(request.data, request.header_end, kMaximumBodyBytes, local_ip.c_str(),
                                                 request.method, sizeof(request.method), request.path, sizeof(request.path),
                                                 request.content_length, has_content_length);
                if (!parsed) return false;
            }
            if (request.header_end && request.used >= request.header_end + request.content_length) return true;
        }
        delay(1U);
    }
    return request.header_end && request.used >= request.header_end + request.content_length;
}

String diagnosticsJson() {
    JsonDocument doc;
    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    const uint64_t uptime_ms = diagnosticUptimeMs();
    const float temperature_c = temperatureRead();
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    doc["system"]["model"] = ESP.getChipModel();
    doc["system"]["revision"] = ESP.getChipRevision();
    doc["system"]["cores"] = ESP.getChipCores();
    doc["system"]["cpu_mhz"] = getCpuFrequencyMhz();
    if (std::isfinite(temperature_c)) doc["system"]["temperature_c"] = temperature_c;
    else doc["system"]["temperature_c"] = nullptr;
    doc["system"]["uptime_ms"] = uptime_ms;
    doc["system"]["reset_reason"] = resetReasonName(esp_reset_reason());
    doc["system"]["wake_cause"] = wakeCauseName(esp_sleep_get_wakeup_cause());
    doc["system"]["sdk"] = ESP.getSdkVersion();
    doc["memory"]["free_heap"] = ESP.getFreeHeap();
    doc["memory"]["minimum_free_heap"] = ESP.getMinFreeHeap();
    doc["memory"]["largest_block"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    doc["memory"]["psram_size"] = ESP.getPsramSize();
    doc["memory"]["free_psram"] = ESP.getFreePsram();
    doc["flash"]["size"] = ESP.getFlashChipSize();
    doc["flash"]["speed_mhz"] = ESP.getFlashChipSpeed() / 1000000U;
    doc["flash"]["sketch_size"] = ESP.getSketchSize();
    doc["flash"]["sketch_partition_size"] = running_partition ? running_partition->size : 0U;
    doc["flash"]["next_ota_partition_size"] = ESP.getFreeSketchSpace();
    doc["wifi"]["ssid"] = WiFi.SSID();
    doc["wifi"]["rssi_dbm"] = WiFi.RSSI();
    doc["wifi"]["channel"] = WiFi.channel();
    doc["wifi"]["ip"] = WiFi.localIP().toString();
    doc["wifi"]["gateway"] = WiFi.gatewayIP().toString();
    doc["wifi"]["subnet"] = WiFi.subnetMask().toString();
    doc["wifi"]["dns"] = WiFi.dnsIP().toString();
    doc["wifi"]["mac"] = WiFi.macAddress();
    doc["wifi"]["bssid"] = WiFi.BSSIDstr();
    doc["network"]["requests"] = request_count;
    doc["i2c"]["sda"] = DIAG_SDA_PIN;
    doc["i2c"]["scl"] = DIAG_SCL_PIN;
    doc["i2c"]["started"] = i2c_scan.started;
    doc["i2c"]["total"] = i2c_scan.total;
    doc["i2c"]["truncated"] = i2c_scan.truncated;
    doc["i2c"]["completed_uptime_ms"] = i2c_scan.completed_uptime_ms;
    JsonArray addresses = doc["i2c"]["addresses"].to<JsonArray>();
    for (uint8_t index = 0U; index < i2c_scan.count; ++index) {
        JsonObject item = addresses.add<JsonObject>();
        char address[5];
        std::snprintf(address, sizeof(address), "0x%02X", i2c_scan.addresses[index]);
        item["hex"] = address;
        item["name"] = knownI2cDeviceName(i2c_scan.addresses[index]);
    }
    doc["spi"]["sck"] = DIAG_SPI_SCK_PIN;
    doc["spi"]["miso"] = DIAG_SPI_MISO_PIN;
    doc["spi"]["mosi"] = DIAG_SPI_MOSI_PIN;
    doc["spi"]["ss"] = DIAG_SPI_SS_PIN;
    doc["interfaces"]["gpio_count"] = SOC_GPIO_PIN_COUNT;
    doc["interfaces"]["uart_count"] = SOC_UART_NUM;
    doc["interfaces"]["spi_count"] = SOC_SPI_PERIPH_NUM;
    doc["interfaces"]["i2c_count"] = SOC_I2C_NUM;
    doc["interfaces"]["wifi"] = (chip.features & CHIP_FEATURE_WIFI_BGN) ? "2.4 GHz 802.11 b/g/n" : "Unavailable";
    doc["interfaces"]["bluetooth"] = (chip.features & CHIP_FEATURE_BLE) ? "BLE available" : "Unavailable";
    String output;
    if (doc.overflowed() || !output.reserve(3072U) || serializeJson(doc, output) == 0U) {
        return F("{\"error\":\"diagnostics_unavailable\"}");
    }
    return output;
}

void handleProvisioningClient(WiFiClient& client, HttpRequest& request) {
    char method[8]{};
    char target[64]{};
    size_t content_length = 0U;
    bool has_content_length = false;
    if (!parseBoundedHttpRequest(request.data, request.header_end, kMaximumBodyBytes, method, sizeof(method), target, sizeof(target), content_length, has_content_length)) {
        sendText(client, 400, "Bad Request", "text/plain; charset=utf-8", "Request rejected");
        return;
    }
    if (!std::strcmp(method, "GET") && !std::strcmp(target, "/")) {
        sendText(client, 200, "OK", "text/html; charset=utf-8", setupPage());
        return;
    }
    if (std::strcmp(method, "POST") || std::strcmp(target, "/save") || !has_content_length || content_length != request.content_length) {
        sendText(client, 404, "Not Found", "text/plain; charset=utf-8", "Not found");
        return;
    }
    StoredConfig candidate{};
    candidate.magic = kConfigMagic;
    candidate.version = kConfigVersion;
    if (!parseDiagnosticSetupForm(request.data + request.header_end, request.content_length, csrf_token,
                                  candidate.ssid, sizeof(candidate.ssid), candidate.password, sizeof(candidate.password))) {
        sendText(client, 400, "Bad Request", "text/html; charset=utf-8", setupPage("Check the Wi-Fi fields and try again."));
        return;
    }
    if (!storeConfig("pending", candidate)) {
        sendText(client, 503, "Unavailable", "text/html; charset=utf-8", setupPage("Configuration could not be stored."));
        return;
    }
    sendText(client, 200, "OK", "text/html; charset=utf-8", setupPage("Saved. The board is restarting to verify Wi-Fi."));
    restart_requested = true;
}

void handleDashboardClient(WiFiClient& client, HttpRequest& request) {
    ++request_count;
    if (!std::strcmp(request.method, "GET") && !std::strcmp(request.path, "/")) {
        String page(FPSTR(kDashboard));
        page.replace("__TOKEN__", dashboard_token);
        sendText(client, 200, "OK", "text/html; charset=utf-8", page);
    } else if (!std::strcmp(request.method, "GET") && !std::strcmp(request.path, "/api/diagnostics")) {
        sendText(client, 200, "OK", "application/json", diagnosticsJson());
    } else if (!std::strcmp(request.method, "POST") && !std::strcmp(request.path, "/api/i2c/scan")) {
        const String expected = String("token=") + dashboard_token;
        const bool valid = request.content_length == expected.length() &&
                           std::memcmp(request.data + request.header_end, expected.c_str(), expected.length()) == 0;
        if (!valid) sendText(client, 403, "Forbidden", "text/plain; charset=utf-8", "Token rejected");
        else if (!diagnosticScanAllowed(millis(), last_scan_started_ms, has_scan_started, kI2cScanCooldownMs)) {
            sendText(client, 429, "Too Many Requests", "text/plain; charset=utf-8", "Wait ten seconds between I2C scans");
        }
        else {
            i2c_scan = scanI2c();
            sendText(client, 200, "OK", "application/json", diagnosticsJson());
        }
    } else sendText(client, 404, "Not Found", "text/plain; charset=utf-8", "Not found");
}

bool startProvisioning() {
    char password[kSetupPasswordCharacters + 1U];
    char ssid[32];
    uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    std::snprintf(ssid, sizeof(ssid), "ESP-Diagnostics-%02X%02X%02X", mac[3], mac[4], mac[5]);
    WiFi.mode(WIFI_AP);
    makeReadableSetupPassword(password);
    randomHex(csrf_token, 8U);
    if (!WiFi.softAP(ssid, password, 1, false, 1)) return false;
    if (!dns.start(53, "*", WiFi.softAPIP())) { WiFi.softAPdisconnect(true); return false; }
    server.begin();
    if (!server) {
        dns.stop();
        WiFi.softAPdisconnect(true);
        return false;
    }
    provisioning = true;
    provisioning_started_ms = millis();
    Serial.printf("Join %s with password %s, then open the setup page at " "http" "://192.168.4.1/\n", ssid, password);
    return true;
}

[[noreturn]] void restartAfterFailure(const char* message) {
    Serial.println(message);
    delay(1000U);
    ESP.restart();
    while (true) delay(1000U);
}

bool clearStoredConfiguration() {
    const bool active_removed = removeConfig("active");
    const bool pending_removed = removeConfig("pending");
    return active_removed && pending_removed;
}

bool setupRequested() {
    pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
    Serial.println("Hold BOOT now for 1.5 seconds to reopen Wi-Fi setup");
    const uint32_t started = millis();
    uint32_t held_since = 0U;
    while (static_cast<uint32_t>(millis() - started) < kRecoveryWindowMs) {
        if (digitalRead(SETUP_BUTTON_PIN) == LOW) {
            if (held_since == 0U) held_since = millis();
            if (static_cast<uint32_t>(millis() - held_since) >= kRecoveryHoldMs) return true;
        } else held_since = 0U;
        delay(10U);
    }
    return false;
}

bool startDashboard() {
    randomHex(dashboard_token, 8U);
    i2c_scan = scanI2c();
    server.begin();
    if (!server) return false;
    Serial.printf("Diagnostics dashboard ready at %s\n", WiFi.localIP().toString().c_str());
    return true;
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200U);
    const bool active_ready = loadConfig("active", active_config);
    StoredConfig pending{};
    const bool pending_ready = loadConfig("pending", pending);
    if ((active_ready || pending_ready) && setupRequested()) {
        if (!clearStoredConfiguration()) restartAfterFailure("Could not erase saved Wi-Fi configuration; restarting");
        if (!startProvisioning()) restartAfterFailure("Could not start protected setup network; restarting");
        return;
    }
    if (pending_ready && active_ready && std::memcmp(&pending, &active_config, sizeof(pending)) == 0) {
        if (!removeConfig("pending")) Serial.println("Active settings are valid; stale pending cleanup will retry after reboot");
    } else if (pending_ready) {
        if (connectWifi(pending) && storeConfig("active", pending)) {
            active_config = pending;
            removeConfig("pending");
            if (!startDashboard()) restartAfterFailure("Could not start diagnostics HTTP listener; restarting");
            return;
        }
        removeConfig("pending");
    }
    if (active_ready && connectWifi(active_config)) {
        if (!startDashboard()) restartAfterFailure("Could not start diagnostics HTTP listener; restarting");
        return;
    }
    if (!startProvisioning()) restartAfterFailure("Could not start protected setup network; restarting");
}

void loop() {
    diagnosticUptimeMs();
    if (provisioning) dns.processNextRequest();
    WiFiClient client = server.accept();
    if (client) {
        const uint32_t now = millis();
        if (static_cast<uint32_t>(now - request_window_started_ms) >= 1000U) {
            request_window_started_ms = now;
            requests_in_window = 0U;
        }
        if (requests_in_window >= kMaximumRequestsPerSecond) {
            sendText(client, 429, "Too Many Requests", "text/plain; charset=utf-8", "Request rate exceeded");
            client.stop();
            delay(2U);
            return;
        }
        ++requests_in_window;
        client.setConnectionTimeout(kResponseDeadlineMs);
        client.setTimeout(kRequestTimeoutMs);
        HttpRequest request;
        if (readRawRequest(client, request)) {
            if (provisioning) handleProvisioningClient(client, request);
            else handleDashboardClient(client, request);
        } else sendText(client, 413, "Payload Too Large", "text/plain; charset=utf-8", "Request incomplete or too large");
        client.stop();
    }
    if (restart_requested) { delay(250U); ESP.restart(); }
    if (provisioning && static_cast<uint32_t>(millis() - provisioning_started_ms) >= kProvisioningTimeoutMs) ESP.restart();
    delay(2U);
}

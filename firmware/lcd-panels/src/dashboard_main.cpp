#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#include <atomic>
#include <cmath>
#include <cstring>

#include "bounded_http.h"
#include "bounded_mqtt.h"
#include "dashboard_config.h"
#include "dashboard_logic.h"
#include "dashboard_space.h"
#include "panel_ca.h"
#include "panel_lcd.h"
#include "panel_logic.h"

namespace {
constexpr uint32_t kHttpDeadlineMs = 8000U;
constexpr size_t kMaximumResponse = 2048U;
constexpr uint32_t kHttpRetryBackoffMs = 300000U;
struct Frame { char top[17]{}; char bottom[17]{}; };
struct FetchArguments { const char* url; char* output; size_t capacity; QueueHandle_t completed; bool has_location; double latitude; double longitude; };
struct RefreshJob { DashboardScreen screen; QueueHandle_t completed; };

DashboardConfig active_config{};
bool active_ready = false;
PanelLcd1602 lcd;
bool lcd_ready = false;
BoundedMqttClient mqtt;
Frame frames[kDashboardScreenCount]{};
size_t active_slot = 0U;
uint32_t slot_started_ms = 0U;
uint32_t last_lcd_attempt_ms = 0U;
uint32_t last_wifi_attempt_ms = 0U;
uint32_t last_mqtt_attempt_ms = 0U;
uint32_t last_clock_ms = 0U;
uint32_t last_satellite_ms = 0U;
uint32_t last_weather_ms = 0U;
uint32_t last_space_summary_ms = 0U;
uint32_t last_mqtt_message_ms = 0U;
uint32_t last_show_ms = 0U;
bool mqtt_message_received = false;
std::atomic<bool> runtime_refresh_active{false};
RTC_DATA_ATTR uint8_t timed_out_screen_marker = 0U;
uint32_t suppression_started_ms = 0U;
std::atomic<bool> fresh_ntp_sync{false};
portMUX_TYPE frame_mux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t refresh_completed = nullptr;
TaskHandle_t refresh_task_handle = nullptr;
RefreshJob refresh_job{};

Frame& frame(DashboardScreen screen) { return frames[static_cast<uint8_t>(screen)]; }
void storeFrame(DashboardScreen screen, const char top[17], const char bottom[17]) { portENTER_CRITICAL(&frame_mux);std::memcpy(frame(screen).top,top,17U);std::memcpy(frame(screen).bottom,bottom,17U);portEXIT_CRITICAL(&frame_mux); }

void fetchTask(void* raw) { auto* a=static_cast<FetchArguments*>(raw);const bool ok=a->has_location?panelHttpGetBoundedAt(a->url,PANEL_PUBLIC_ROOTS,a->latitude,a->longitude,a->output,a->capacity):panelHttpGetBounded(a->url,PANEL_PUBLIC_ROOTS,a->output,a->capacity);xQueueSend(a->completed,&ok,0U);vTaskSuspend(nullptr); }
bool fetchRequest(FetchArguments& a){QueueHandle_t q=xQueueCreate(1U,sizeof(bool));if(!q)return false;a.completed=q;TaskHandle_t task=nullptr;if(xTaskCreate(fetchTask,"dash-http",10240U,&a,1U,&task)!=pdPASS){vQueueDelete(q);return false;}bool ok=false;if(xQueueReceive(q,&ok,pdMS_TO_TICKS(kHttpDeadlineMs))!=pdTRUE){vTaskDelete(task);vQueueDelete(q);if(runtime_refresh_active.load(std::memory_order_acquire))timed_out_screen_marker=static_cast<uint8_t>(refresh_job.screen)+1U;Serial.println("Dashboard HTTP deadline exceeded; restarting with endpoint backoff");delay(50);ESP.restart();for (;;) delay(1000);}vTaskDelete(task);vQueueDelete(q);return ok;}
bool fetchUrl(const char* url,char* output,size_t capacity){FetchArguments a{url,output,capacity,nullptr,false,0.0,0.0};return fetchRequest(a);}
bool fetchUrlAt(const char* url,double latitude,double longitude,char* output,size_t capacity){FetchArguments a{url,output,capacity,nullptr,true,latitude,longitude};return fetchRequest(a);}
bool connectWifi(const DashboardConfig& value,uint32_t timeout_ms){WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.begin(value.sources.wifi_ssid,value.sources.wifi_password);const uint32_t started=millis();while(WiFi.status()!=WL_CONNECTED&&static_cast<uint32_t>(millis()-started)<timeout_ms)delay(100);return WiFi.status()==WL_CONNECTED&&WiFi.localIP()!=IPAddress(0U,0U,0U,0U);}
bool ensureTime(const char* timezone){if(time(nullptr)>=1704067200)return true;configTzTime(timezone,"pool.ntp.org","time.nist.gov");const uint32_t started=millis();while(time(nullptr)<1704067200&&static_cast<uint32_t>(millis()-started)<10000U)delay(100);return time(nullptr)>=1704067200;}
void onNtpSync(struct timeval*){fresh_ntp_sync.store(true,std::memory_order_release);}
bool synchronizeFresh(const char* timezone){esp_sntp_stop();fresh_ntp_sync.store(false,std::memory_order_release);sntp_set_time_sync_notification_cb(onNtpSync);configTzTime(timezone,"pool.ntp.org","time.nist.gov");const uint32_t started=millis();while(!fresh_ntp_sync.load(std::memory_order_acquire)&&static_cast<uint32_t>(millis()-started)<10000U)delay(50);return fresh_ntp_sync.load(std::memory_order_acquire)&&time(nullptr)>=1704067200;}
bool fresh(long long timestamp,long long tolerance){const long long now=static_cast<long long>(time(nullptr));if(timestamp<1704067200LL||timestamp>4102444800LL||now<1704067200LL||now>4102444800LL)return false;return (now>=timestamp?now-timestamp:timestamp-now)<=tolerance;}
void unavailable(DashboardScreen screen){char top[17]{},bottom[17]{};switch(screen){case DashboardScreen::Clock:formatMqttText("NTP Desk Clock","Time unavailable",top,bottom);break;case DashboardScreen::Mqtt:formatMqttText("MQTT Home","Waiting for data",top,bottom);break;case DashboardScreen::Satellite:formatSatellite(NAN,NAN,-1,top,bottom);break;case DashboardScreen::Weather:formatWeather(NAN,-1,nullptr,top,bottom);break;case DashboardScreen::Launch:formatMqttText("Next Launch","Unavailable",top,bottom);break;case DashboardScreen::Moon:formatMqttText("Moon","Unavailable",top,bottom);break;case DashboardScreen::Solar:formatMqttText("Solar Activity","Unavailable",top,bottom);break;case DashboardScreen::Planet:formatMqttText("Planet View","Unavailable",top,bottom);break;case DashboardScreen::Neo:formatMqttText("Near-Earth Obj","Unavailable",top,bottom);break;case DashboardScreen::DeepSpace:formatMqttText("Deep Space","Unavailable",top,bottom);break;}storeFrame(screen,top,bottom);}
void initializeFrames(){for(uint8_t i=0U;i<kDashboardScreenCount;++i)unavailable(static_cast<DashboardScreen>(i));}
void staleMqtt(){char top[17]{},bottom[17]{};formatMqttText(active_config.sources.label,"Data stale",top,bottom);storeFrame(DashboardScreen::Mqtt,top,bottom);}
void updateClock(){struct tm local{};if(!getLocalTime(&local,50U)||local.tm_year<120){unavailable(DashboardScreen::Clock);return;}char top[17]{},bottom[17]{};std::snprintf(top,sizeof(top),"Date %04d-%02d-%02d",local.tm_year+1900,local.tm_mon+1,local.tm_mday);std::snprintf(bottom,sizeof(bottom),"Time %02d:%02d:%02d",local.tm_hour,local.tm_min,local.tm_sec);storeFrame(DashboardScreen::Clock,top,bottom);}
bool fetchWeather(const DashboardConfig& value){if(!ensureTime(value.timezone))return false;char path[192]{};if(!buildWeatherPath(std::strtod(value.sources.latitude,nullptr),std::strtod(value.sources.longitude,nullptr),path,sizeof(path)))return false;char url[256]{};const int n=std::snprintf(url,sizeof(url),"https://api.open-meteo.com%s",path);if(n<=0||static_cast<size_t>(n)>=sizeof(url))return false;char body[kMaximumResponse+1U]{};if(!fetchUrl(url,body,sizeof(body)))return false;JsonDocument d;if(deserializeJson(d,body)!=DeserializationError::Ok)return false;JsonVariant c=d["current"];if(!c.is<JsonObject>())return false;const float t=c["temperature_2m"]|NAN;const int h=c["relative_humidity_2m"]|-1;const int code=c["weather_code"]|-1;const long long timestamp=c["time"]|0LL;if(!std::isfinite(t)||t<-100.0F||t>100.0F||h<0||h>100||code<0||!fresh(timestamp,3600LL))return false;char top[17]{},bottom[17]{};formatWeather(t,h,weatherCondition(code),top,bottom);storeFrame(DashboardScreen::Weather,top,bottom);return true;}
bool fetchSatellite(const DashboardConfig& value){if(!ensureTime(value.timezone))return false;char body[kMaximumResponse+1U]{};if(!fetchUrl("https://api.wheretheiss.at/v1/satellites/25544",body,sizeof(body)))return false;JsonDocument d;if(deserializeJson(d,body)!=DeserializationError::Ok||(d["id"]|0)!=25544)return false;const double lat=d["latitude"]|NAN,lon=d["longitude"]|NAN,alt=d["altitude"]|NAN;const long long timestamp=d["timestamp"]|0LL;if(!std::isfinite(lat)||lat< -90.0||lat>90.0||!std::isfinite(lon)||lon< -180.0||lon>180.0||!std::isfinite(alt)||alt<100.0||alt>1000.0||!fresh(timestamp,120LL))return false;char top[17]{},bottom[17]{};formatSatellite(lat,lon,static_cast<int>(std::lround(alt)),top,bottom);storeFrame(DashboardScreen::Satellite,top,bottom);return true;}

bool fetchSpaceSummary(const DashboardConfig& value){if(!ensureTime(value.timezone))return false;const double latitude=std::strtod(value.sources.latitude,nullptr),longitude=std::strtod(value.sources.longitude,nullptr);char body[kMaximumResponse+1U]{};if(!fetchUrlAt("https://api.justsome.space/v1/lcd/space",latitude,longitude,body,sizeof(body)))return false;JsonDocument d;if(deserializeJson(d,body)!=DeserializationError::Ok||!dashboardSpaceResponseValid(d,static_cast<long long>(time(nullptr))))return false;JsonObject data=d["data"].as<JsonObject>();char top[17]{},bottom[17]{};
auto launch=data["launch"];if(launch["available"]|false){const char* name=launch["name"]|"";const long long net=launch["net"]|0LL;const long long remaining=net-static_cast<long long>(time(nullptr));if(*name!='\0'&&net>=1704067200LL){if(remaining>0)std::snprintf(bottom,sizeof(bottom),"T-%lldd %02lldh",remaining/86400LL,(remaining/3600LL)%24LL);else std::snprintf(bottom,sizeof(bottom),"Launch window");formatMqttText(name,bottom,top,bottom);storeFrame(DashboardScreen::Launch,top,bottom);}else unavailable(DashboardScreen::Launch);}else unavailable(DashboardScreen::Launch);
auto moon=data["moon"];if(moon["available"]|false){const char* phase=moon["phase"]|"";const int illumination=moon["illumination"]|-1;if(*phase!='\0'&&illumination>=0&&illumination<=100){std::snprintf(bottom,sizeof(bottom),"Illumination %d%%",illumination);formatMqttText(phase,bottom,top,bottom);storeFrame(DashboardScreen::Moon,top,bottom);}else unavailable(DashboardScreen::Moon);}else unavailable(DashboardScreen::Moon);
auto solar=data["solar"];if(solar["available"]|false){const double kp=solar["kp"]|NAN;const char* level=solar["level"]|"";if(std::isfinite(kp)&&kp>=0.0&&kp<=9.0&&*level!='\0'){std::snprintf(top,sizeof(top),"Solar Kp %.1f",kp);formatMqttText(top,level,top,bottom);storeFrame(DashboardScreen::Solar,top,bottom);}else unavailable(DashboardScreen::Solar);}else unavailable(DashboardScreen::Solar);
auto planet=data["planet"];if(planet["available"]|false){const char* name=planet["name"]|"";const int altitude=planet["altitude"]|-91,azimuth=planet["azimuth"]|-1;const char* direction=planet["direction"]|"";if(*name!='\0'&&altitude>=-90&&altitude<=90&&azimuth>=0&&azimuth<=360&&*direction!='\0'){std::snprintf(top,sizeof(top),"%s %d deg",name,altitude);std::snprintf(bottom,sizeof(bottom),"%s az %d",direction,azimuth);formatMqttText(top,bottom,top,bottom);storeFrame(DashboardScreen::Planet,top,bottom);}else unavailable(DashboardScreen::Planet);}else unavailable(DashboardScreen::Planet);
auto neo=data["neo"];if(neo["available"]|false){const char* name=neo["name"]|"";const double distance=neo["distance_ld"]|NAN;const long long approach=neo["approach_at"]|0LL;if(*name!='\0'&&std::isfinite(distance)&&distance>=0.0&&distance<=1000.0&&approach>=1704067200LL){std::snprintf(bottom,sizeof(bottom),"%.1f lunar dist",distance);formatMqttText(name,bottom,top,bottom);storeFrame(DashboardScreen::Neo,top,bottom);}else unavailable(DashboardScreen::Neo);}else unavailable(DashboardScreen::Neo);
auto mission=data["mission"];if(mission["available"]|false){const char* name=mission["name"]|"";const double distance=mission["distance_au"]|NAN;const int days=mission["mission_days"]|-1;if(*name!='\0'&&std::isfinite(distance)&&distance>0.0&&distance<10000.0&&days>=0){std::snprintf(bottom,sizeof(bottom),"%.1f AU %dd",distance,days);formatMqttText(name,bottom,top,bottom);storeFrame(DashboardScreen::DeepSpace,top,bottom);}else unavailable(DashboardScreen::DeepSpace);}else unavailable(DashboardScreen::DeepSpace);return true;}

void mqttCallback(const uint8_t* payload,size_t length){if(length==0U||length>256U)return;char raw[257]{};for(size_t i=0U;i<length;++i){const unsigned char c=payload[i];raw[i]=c>=0x20U&&c<=0x7eU?static_cast<char>(c):' ';}char value[64]{};JsonDocument d;const bool json=deserializeJson(d,payload,length)==DeserializationError::Ok;if(json&&d["temperature_c"].is<float>()&&d["humidity_percent"].is<float>()){const double t=d["temperature_c"].as<double>(),h=d["humidity_percent"].as<double>();if(std::isfinite(t)&&t>=-100.0&&t<=100.0&&std::isfinite(h)&&h>=0.0&&h<=100.0)std::snprintf(value,sizeof(value),"%.1f C / %.0f%%",t,h);else std::snprintf(value,sizeof(value),"Invalid reading");}else if(json&&d["value"].is<const char*>())std::snprintf(value,sizeof(value),"%s",d["value"].as<const char*>());else std::snprintf(value,sizeof(value),"%s",raw);char top[17]{},bottom[17]{};formatMqttText(active_config.sources.label,value,top,bottom);storeFrame(DashboardScreen::Mqtt,top,bottom);last_mqtt_message_ms=millis();mqtt_message_received=true;}
bool validateCandidate(const DashboardConfig& candidate){if(!connectWifi(candidate,15000U)||!synchronizeFresh(candidate.timezone))return false;BoundedMqttClient test;const bool mqtt_ok=test.connect(candidate.sources,mqttCallback);test.stop();return mqtt_ok && fetchWeather(candidate);}
void refreshWorker(void* raw){auto* job=static_cast<RefreshJob*>(raw);runtime_refresh_active=true;bool ok=false;if(job->screen==DashboardScreen::Weather)ok=fetchWeather(active_config);else if(job->screen==DashboardScreen::Satellite)ok=fetchSatellite(active_config);else if(job->screen==DashboardScreen::Launch)ok=fetchSpaceSummary(active_config);runtime_refresh_active=false;if(ok&&timed_out_screen_marker==static_cast<uint8_t>(job->screen)+1U)timed_out_screen_marker=0U;if(!ok&&job->screen!=DashboardScreen::Launch)unavailable(job->screen);if(xQueueSend(job->completed,&ok,0U)!=pdTRUE){Serial.println("Dashboard refresh completion queue failed");delay(50);ESP.restart();}vTaskSuspend(nullptr);}
bool startRefresh(DashboardScreen screen){refresh_job={screen,refresh_completed};return xTaskCreate(refreshWorker,"dash-refresh",12288U,&refresh_job,1U,&refresh_task_handle)==pdPASS;}
bool refreshAllowed(DashboardScreen screen,uint32_t now){return timed_out_screen_marker!=static_cast<uint8_t>(screen)+1U||static_cast<uint32_t>(now-suppression_started_ms)>=kHttpRetryBackoffMs;}
void serviceRefresh(uint32_t now){if(refresh_task_handle!=nullptr){bool complete=false;if(xQueueReceive(refresh_completed,&complete,0U)==pdTRUE){vTaskDelete(refresh_task_handle);refresh_task_handle=nullptr;}return;}if(WiFi.status()!=WL_CONNECTED)return;DashboardScreen due{};bool selected=false;if(refreshAllowed(DashboardScreen::Weather,now)&&static_cast<uint32_t>(now-last_weather_ms)>=600000U){last_weather_ms=now;due=DashboardScreen::Weather;selected=true;}else if(refreshAllowed(DashboardScreen::Launch,now)&&static_cast<uint32_t>(now-last_space_summary_ms)>=300000U){last_space_summary_ms=now;due=DashboardScreen::Launch;selected=true;}else if(refreshAllowed(DashboardScreen::Satellite,now)&&static_cast<uint32_t>(now-last_satellite_ms)>=10000U){last_satellite_ms=now;due=DashboardScreen::Satellite;selected=true;}if(selected&&!startRefresh(due)&&due!=DashboardScreen::Launch)unavailable(due);}
void show(){const uint32_t now=millis();if(!lcd_ready&&static_cast<uint32_t>(now-last_lcd_attempt_ms)>=5000U){last_lcd_attempt_ms=now;lcd_ready=lcd.begin();}if(!lcd_ready||static_cast<uint32_t>(now-last_show_ms)<250U)return;last_show_ms=now;const DashboardScreen screen=active_config.schedule.order[active_slot];Frame copy{};portENTER_CRITICAL(&frame_mux);copy=frame(screen);portEXIT_CRITICAL(&frame_mux);if(!lcd.show(copy.top,copy.bottom))lcd_ready=false;}
}  // namespace

void setup(){Serial.begin(115200);initializeFrames();lcd_ready=lcd.begin();pinMode(SETUP_BUTTON_PIN,INPUT_PULLUP);active_ready=dashboardLoadConfig("active",active_config);DashboardConfig pending{};const bool pending_valid=dashboardLoadConfig("pending",pending);const bool pending_matches=pending_valid&&active_ready&&std::memcmp(&pending,&active_config,sizeof(pending))==0;const bool pending_ready=pending_valid&&!pending_matches&&!dashboardPendingSuppressed();if(!pending_valid)dashboardRemoveConfig("pending");else if(pending_matches)dashboardRemoveConfig("pending");if((active_ready||pending_ready)&&dashboardRecoveryRequested()){if(!dashboardStartProvisioning()){delay(30000);ESP.restart();}return;}if(pending_ready){dashboardBeginPendingValidation();if(validateCandidate(pending)&&dashboardPromotePending(pending)){active_config=pending;active_ready=true;Serial.println("Dashboard settings verified and promoted");}else{dashboardRejectPending();WiFi.disconnect(true,false);Serial.println("Dashboard settings rejected; active settings preserved");}}if(!active_ready){if(!dashboardStartProvisioning()){delay(30000);ESP.restart();}return;}if(WiFi.status()!=WL_CONNECTED)connectWifi(active_config,15000U);configTzTime(active_config.timezone,"pool.ntp.org","time.nist.gov");mqtt.connect(active_config.sources,mqttCallback);refresh_completed=xQueueCreate(1U,sizeof(bool));if(refresh_completed==nullptr){Serial.println("Dashboard refresh queue allocation failed");delay(30000);ESP.restart();return;}const uint32_t ready_ms=millis();suppression_started_ms=ready_ms;last_weather_ms=ready_ms-600000U;last_space_summary_ms=ready_ms-300000U;last_satellite_ms=ready_ms-10000U;slot_started_ms=ready_ms;show();}

void loop(){if(dashboardProvisioningActive()){dashboardHandleProvisioning();delay(2);return;}const uint32_t now=millis();if(WiFi.status()!=WL_CONNECTED&&static_cast<uint32_t>(now-last_wifi_attempt_ms)>=30000U){last_wifi_attempt_ms=now;WiFi.begin(active_config.sources.wifi_ssid,active_config.sources.wifi_password);}if(WiFi.status()==WL_CONNECTED&&!mqtt.connected()&&static_cast<uint32_t>(now-last_mqtt_attempt_ms)>=10000U){last_mqtt_attempt_ms=now;mqtt.connect(active_config.sources,mqttCallback);}if(mqtt.connected())mqtt.loop();const uint32_t after_mqtt=millis();if(mqttMessageStale(after_mqtt,last_mqtt_message_ms,mqtt_message_received)){staleMqtt();mqtt_message_received=false;}if(static_cast<uint32_t>(now-last_clock_ms)>=1000U){last_clock_ms=now;updateClock();}serviceRefresh(millis());const uint32_t current=millis();if(dashboardSlotExpired(slot_started_ms,current,dashboardDurationForSlot(active_config.schedule,active_slot))){active_slot=dashboardNextSlot(active_config.schedule,active_slot);slot_started_ms=current;}show();delay(50);}

#include <Arduino.h>
// --- ディスプレイ2 (円形LCD) 用 ---
#include <lgfx/v1/panel/Panel_GC9A01.hpp>
// --- ディスプレイ1 (M5UnitGLASS2) 用 ---
#include <M5UnitGLASS2.h>
#include <M5Unified.h>
#include <M5GFX.h>

// --- 共通機能用 ---
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <time.h>
#include <FS.h>
#include <SPIFFS.h>
#include <algorithm>
#include "esp_sntp.h"

// =======================================================================
// --- ユーザー設定  ---
// =======================================================================
const char* ssid = "*************";     // your WiFi SSID
const char* password = "************"; // your WiFi PASS
const char* ntpServer = "pool.ntp.org";   // NTPサーバー

// =======================================================================
// --- NeoPixel LED 設定 ---
// =======================================================================
#define LED_PIN    38
#define LED_COUNT  15
#define BRIGHTNESS 150
const uint8_t ledR = 255, ledG = 255, ledB = 255;

// =======================================================================
// --- ディスプレイ1 (GLASS2) 用 設定  ---
// =======================================================================
const float EARTH_RADIUS_GLASS = 28.0f;
const float EARTH_AXIAL_TILT_DEG_GLASS = -23.4f;
const float VIEW_X_ANGLE_DEG_GLASS = 0.0f;
const float VIEW_Y_ANGLE_DEG_GLASS = 30.0f;
const int EARTH_OFFSET_X_GLASS = -30;
const int EARTH_OFFSET_Y_GLASS = 0;
const double SYNODIC_MONTH_GLASS = 29.530588853;
const time_t NEW_MOON_REFERENCE_TIME_GLASS = 947182440;
struct City_Glass { const char* name; float lat; float lon; };
City_Glass cities_glass[] = {
    {"Tokyo", 35.68f, 139.69f},
    {"London", 51.50f, -0.12f},
    {"NewYork", 40.71f, -74.00f}
};
struct Vector3D_Glass { float x, y, z; };
struct Vector2D_Glass { int x, y; float depth; };

// =======================================================================
// --- ディスプレイ2 (円形LCD) のクラス定義  ---
// =======================================================================
class LGFX_LCD : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01  _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;
public:
    LGFX_LCD(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST; cfg.spi_mode = 3;
            cfg.freq_write = 80000000; cfg.freq_read  = 16000000;
            cfg.spi_3wire  = true; cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 5; cfg.pin_mosi = 6;
            cfg.pin_miso = -1; cfg.pin_dc   = 7;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = 8; cfg.pin_rst = -1; cfg.pin_busy = -1;
            cfg.panel_width = 240; cfg.panel_height = 240;
            cfg.offset_x = 0; cfg.offset_y = 0; cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8; cfg.dummy_read_bits = 1;
            cfg.readable = true; cfg.invert = true; cfg.rgb_order = false;
            cfg.dlen_16bit = false; cfg.bus_shared = false;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = 6; cfg.invert = false;
            cfg.freq = 44100; cfg.pwm_channel = 3;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        setPanel(&_panel_instance);
    }
};

// =======================================================================
// --- グローバルオブジェクト定義 ---
// =======================================================================
M5UnitGLASS2 display_glass(2, 1, 400000);
LGFX_LCD display_lcd;
Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
M5Canvas canvas_glass(&display_glass);
LGFX_Sprite* sprite_lcd = nullptr;
LGFX_Sprite* shadow_mask_lcd = nullptr;
int last_updated_minute = -1;

// =======================================================================
// --- 関数宣言 ---
// =======================================================================
void draw_world_on_glass(const struct tm* utc_time);
void draw_world_on_lcd(const struct tm* utc_time);
void updateLedPosition(const struct tm* utc_time);

// =======================================================================
// --- setup() ---
// =======================================================================
void setup() {
    auto cfg = M5.config();
    cfg.clear_display = false;
    cfg.output_power = false;
    M5.begin(cfg);

    SPIFFS.begin(true);

    display_glass.begin();
    canvas_glass.setPsram(false);
    canvas_glass.createSprite(display_glass.width(), display_glass.height());

    display_lcd.begin();
    display_lcd.setRotation(2);
    sprite_lcd = new LGFX_Sprite(&display_lcd);
    sprite_lcd->setColorDepth(16);
    sprite_lcd->createSprite(240, 240);
    sprite_lcd->setPivot(120, 120);
    shadow_mask_lcd = new LGFX_Sprite(&display_lcd);
    shadow_mask_lcd->setColorDepth(1);
    shadow_mask_lcd->createSprite(240, 240);
    
    pixels.begin();
    pixels.setBrightness(BRIGHTNESS);
    pixels.clear();
    pixels.show();

    display_lcd.fillScreen(TFT_BLACK);
    display_lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    display_lcd.setTextSize(2);
    display_lcd.setCursor(20, 100);
    display_lcd.println("Connecting WiFi...");

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        display_lcd.print(".");
    }
    display_lcd.fillScreen(TFT_BLACK);
    display_lcd.println("Syncing Time...");

    configTime(0, 0, ntpServer);
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo, 5000) || timeinfo.tm_year < 100) {
        delay(100);
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    display_lcd.fillScreen(TFT_BLACK);
}

// =======================================================================
// --- loop() ---
// =======================================================================
void loop() {
    M5.update();
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    if (timeinfo.tm_wday == 0 && timeinfo.tm_hour == 3 && timeinfo.tm_min == 0) {
        ESP.restart();
    }

    draw_world_on_glass(&timeinfo);

    if (timeinfo.tm_min != last_updated_minute) {
        last_updated_minute = timeinfo.tm_min;
        draw_world_on_lcd(&timeinfo);
    }

    updateLedPosition(&timeinfo);
    delay(1000);
}

// =======================================================================
// --- ディスプレイ1 (GLASS2) 関連の関数群 ---
// =======================================================================
Vector2D_Glass project_glass(Vector3D_Glass pos, float rot_rad, float tilt_rad, float vx_rad, float vy_rad) {
    float rot_cos = cos(rot_rad), rot_sin = sin(rot_rad);
    float x_r = pos.x * rot_cos - pos.y * rot_sin, y_r = pos.x * rot_sin + pos.y * rot_cos;
    float tilt_cos = cos(-tilt_rad), tilt_sin = sin(-tilt_rad);
    float y_t = y_r * tilt_cos - pos.z * tilt_sin, z_t = y_r * tilt_sin + pos.z * tilt_cos;
    float view_x_cos = cos(vx_rad), view_x_sin = sin(vx_rad);
    float y_vx = y_t * view_x_cos - z_t * view_x_sin, z_vx = y_t * view_x_sin + z_t * view_x_cos;
    float view_y_cos = cos(vy_rad), view_y_sin = sin(vy_rad);
    float x_vy = x_r * view_y_cos + z_vx * view_y_sin, z_vy = -x_r * view_y_sin + z_vx * view_y_cos;
    Vector2D_Glass p;
    p.x = y_vx + (display_glass.width() / 2) + EARTH_OFFSET_X_GLASS;
    p.y = -z_vy + (display_glass.height() / 2) + EARTH_OFFSET_Y_GLASS;
    p.depth = x_vy;
    return p;
}
Vector3D_Glass sphericalToCartesian_glass(float lat, float lon, float r) {
    float latRad = lat * PI / 180.0f, lonRad = lon * PI / 180.0f;
    Vector3D_Glass v;
    v.x = r * cos(latRad) * cos(lonRad); v.y = r * cos(latRad) * sin(lonRad); v.z = r * sin(latRad);
    return v;
}

void getLocalTime(struct tm* timeinfo, const City_Glass* city) { // timeinfoから月、日、曜日を取得 // サマータイムロジックで使うため、月は1-12、曜日は0-6(日曜-土曜)に調整

int currentMonth = timeinfo->tm_mon + 1;
int currentDay = timeinfo->tm_mday;
int currentWeekday = timeinfo->tm_wday;

if (strcmp(city->name, "Tokyo") == 0) {
    // 東京 (JST, UTC+9)
    timeinfo->tm_hour += 9;
}
else if (strcmp(city->name, "London") == 0) {
    // --- ロンドンの夏時間(BST)判定ロジック ---
    bool is_bst = false;       

    // 3月: 最終日曜日に開始
    if (currentMonth == 3) {
        if ((currentDay >= 25 && currentWeekday == 0) ||
            (currentDay >= 26 && currentWeekday == 1) ||
            (currentDay >= 27 && currentWeekday == 2) ||
            (currentDay >= 28 && currentWeekday == 3) ||
            (currentDay >= 29 && currentWeekday == 4) ||
            (currentDay >= 30 && currentWeekday == 5) ||
            (currentDay >= 31 && currentWeekday == 6)) {
            is_bst = true;
        }
    }
    // 4月～9月: 期間内
    else if (currentMonth >= 4 && currentMonth <= 9) {
        is_bst = true;
    }
    // 10月: 最終日曜日に終了
    else if (currentMonth == 10) {
        if ((currentDay <= 24 && currentWeekday == 0) ||
            (currentDay <= 25 && currentWeekday == 1) ||
            (currentDay <= 26 && currentWeekday == 2) ||
            (currentDay <= 27 && currentWeekday == 3) ||
            (currentDay <= 28 && currentWeekday == 4) ||
            (currentDay <= 29 && currentWeekday == 5) || // (注) '39'から'29'へ修正
            (currentDay <= 30 && currentWeekday == 6)) {
            is_bst = true;
        }
    }

    // --- オフセット適用 ---
    // 冬時間(GMT)はUTC+0、夏時間(BST)はUTC+1
    if (is_bst) {
        timeinfo->tm_hour += 1;
    }
}
else if (strcmp(city->name, "NewYork") == 0) {
    // --- ニューヨークの夏時間(DST)判定ロジック ---
    bool is_ny_dst = false;
    // 3月: 第2日曜日に開始
    if (currentMonth == 3) {
        if ((currentDay >= 8  && currentWeekday == 0) ||
            (currentDay >= 9  && currentWeekday == 1) ||
            (currentDay >= 10 && currentWeekday == 2) ||
            (currentDay >= 11 && currentWeekday == 3) ||
            (currentDay >= 12 && currentWeekday == 4) ||
            (currentDay >= 13 && currentWeekday == 5) ||
            (currentDay >= 14 && currentWeekday == 6)) {
            is_ny_dst = true;
        }
    }
    // 4月～10月: 期間内
    else if (currentMonth >= 4 && currentMonth <= 10) {
        is_ny_dst = true;
    }
    // 11月: 第1日曜日に終了
    else if (currentMonth == 11) {
        if ((currentDay <= 7 && currentWeekday == 0) ||

 (currentDay <= 1 && currentWeekday == 1) || (currentDay <= 2 && currentWeekday == 2) || (currentDay <= 3 && currentWeekday == 3) || (currentDay <= 4 && currentWeekday == 4) || (currentDay <= 5 && currentWeekday == 5) || (currentDay <= 6 && currentWeekday == 6)) { is_ny_dst = true; } }

    // --- オフセット適用 ---
    // 冬時間(EST)はUTC-5、夏時間(EDT)はUTC-4
    timeinfo->tm_hour -= 5;
    if (is_ny_dst) {
        timeinfo->tm_hour += 1;
    }
}
// 時間の変更をtm構造体に正規化して反映させる
mktime(timeinfo);
}

void drawMoonInBox_glass(float phase, double rad) {
    int w = display_glass.width(), h = display_glass.height(), bw = 32, bh = 40;
    uint16_t shadow = display_glass.color565(50, 50, 50);
    int bx = w - bw - 1, by = h - bh - 1;
    canvas_glass.drawFastVLine(bx, by, bh, WHITE); canvas_glass.drawFastHLine(bx, by, bw, WHITE);
    canvas_glass.setFont(&fonts::TomThumb); canvas_glass.setTextColor(WHITE);
    canvas_glass.setCursor(bx + 2, by + 2); canvas_glass.print("  Moon");
    canvas_glass.setCursor(bx + 2, by + 8); canvas_glass.print("  phase");
    int cx = bx + bw / 2, cy = by + 14 + (bh - 14) / 2, r = (bw / 2) - 6;
    canvas_glass.fillCircle(cx, cy, r, WHITE);
    if (phase > 0.98) { canvas_glass.fillCircle(cx, cy, r, shadow); } else if (phase < -0.98) {} else {
        bool is_waning = (rad > PI);
        for (int y = -r; y <= r; y++) {
            float xd = sqrt(r * r - y * y);
            if (is_waning) {
                int sx = cx + (int)(xd * -phase), width = (int)(xd * (1 + phase));
                if (width > 0) canvas_glass.drawFastHLine(sx, cy + y, width, shadow);
            } else {
                int sx = cx - (int)xd, width = (int)(xd * (1 + phase));
                if (width > 0) canvas_glass.drawFastHLine(sx, cy + y, width, shadow);
            }
        }
    }
    canvas_glass.setFont(&fonts::Font0);
}
void draw_world_on_glass(const struct tm* utc_time) {
    canvas_glass.fillSprite(BLACK);
    time_t now = time(nullptr);
    float hf = utc_time->tm_hour + utc_time->tm_min / 60.0f + utc_time->tm_sec / 3600.0f;
    float rot_rad = ((hf * 15.0f) - 90.0f) * PI / 180.0f;
    const float tilt_rad = EARTH_AXIAL_TILT_DEG_GLASS * PI / 180.0f;
    const float vx_rad = VIEW_X_ANGLE_DEG_GLASS * PI / 180.0f;
    const float vy_rad = VIEW_Y_ANGLE_DEG_GLASS * PI / 180.0f;
    double moon_rad = fmod(difftime(now, NEW_MOON_REFERENCE_TIME_GLASS) / 86400.0, SYNODIC_MONTH_GLASS) / SYNODIC_MONTH_GLASS * 2.0 * PI;
    drawMoonInBox_glass(cos(moon_rad), moon_rad);
    for (int type = 0; type < 2; ++type) {
        for (int i = (type==0?-60:0); i <= (type==0?60:359); i += 30) {
            Vector2D_Glass p_prev;
            for (int j = 0; j <= (type==0?360:180); j += 5) {
                float lat = (type == 0) ? i : (j - 90), lon = (type == 0) ? j : i;
                if(lat < -85 || lat > 85) continue;
                Vector3D_Glass p3d = sphericalToCartesian_glass(lat, lon, EARTH_RADIUS_GLASS);
                Vector2D_Glass p2d = project_glass(p3d, rot_rad, tilt_rad, vx_rad, vy_rad);
                if (j > 0 && p2d.depth > 0 && p_prev.depth > 0) canvas_glass.drawLine(p_prev.x, p_prev.y, p2d.x, p2d.y, WHITE);
                p_prev = p2d;
            }
        }
    }
    
    const int SCREEN_WIDTH = display_glass.width();
    const int SCREEN_HEIGHT = display_glass.height();
    const int MOON_BOX_WIDTH = 32;
    const int MOON_BOX_HEIGHT = 40;
    struct TextRect { int x, y, w, h; };
    TextRect displayed_texts[3];
    int text_count = 0;
    TextRect moon_area = {SCREEN_WIDTH - MOON_BOX_WIDTH - 1, SCREEN_HEIGHT - MOON_BOX_HEIGHT - 1, MOON_BOX_WIDTH, MOON_BOX_HEIGHT};

    for (const auto& city : cities_glass) {
        Vector3D_Glass p3d = sphericalToCartesian_glass(city.lat, city.lon, EARTH_RADIUS_GLASS);
        Vector2D_Glass p2d = project_glass(p3d, rot_rad, tilt_rad, vx_rad, vy_rad);
        struct tm local_tm; memcpy(&local_tm, utc_time, sizeof(struct tm));
        getLocalTime(&local_tm, &city);
        
        canvas_glass.setFont(&fonts::TomThumb);
        int city_w = canvas_glass.textWidth(city.name), city_h = canvas_glass.fontHeight();
        canvas_glass.setFont(&fonts::Font0); char timeStr[10];
        sprintf(timeStr, "%02d:%02d", local_tm.tm_hour, local_tm.tm_min);
        int time_w = canvas_glass.textWidth(timeStr), time_h = canvas_glass.fontHeight();
        
        int total_w = city_w + 2 + time_w;
        int max_h = max(city_h, time_h);
        int tx = p2d.x + 4, ty = p2d.y - (max_h / 2);

        if (tx + total_w > SCREEN_WIDTH) tx = p2d.x - 4 - total_w;
        if (tx < 0) tx = 0; if (ty < 0) ty = 0;
        if (ty + max_h > SCREEN_HEIGHT) ty = SCREEN_HEIGHT - max_h;

        bool overlapped;
        do {
            overlapped = false;
            for (int i = 0; i < text_count; ++i) {
                if (tx < displayed_texts[i].x + displayed_texts[i].w && tx + total_w > displayed_texts[i].x &&
                    ty < displayed_texts[i].y + displayed_texts[i].h && ty + max_h > displayed_texts[i].y) {
                    ty += displayed_texts[i].h;
                    if (ty + max_h > SCREEN_HEIGHT) {
                        ty = displayed_texts[i].y - max_h;
                        if (ty < 0) ty = 0;
                    }
                    overlapped = true;
                    break;
                }
            }
        } while (overlapped);

        if (tx < moon_area.x + moon_area.w && tx + total_w > moon_area.x &&
            ty < moon_area.y + moon_area.h && ty + max_h > moon_area.y) {
            tx = moon_area.x - total_w - 5;
            if (tx < 0) {
                if (ty > SCREEN_HEIGHT / 2) { ty = moon_area.y - max_h - 2; }
                else { ty = moon_area.y + moon_area.h + 2; }
                tx = SCREEN_WIDTH - total_w - 2;
                if (ty < 0) ty = 0;
                if (ty + max_h > SCREEN_HEIGHT) ty = SCREEN_HEIGHT - max_h;
            }
        }

        displayed_texts[text_count++] = {tx, ty, total_w, max_h};
        
        canvas_glass.fillRect(tx-1, ty, total_w+2, max_h, BLACK);
        canvas_glass.setFont(&fonts::TomThumb); canvas_glass.setTextColor(WHITE);
        canvas_glass.drawString(city.name, tx, ty + (max_h - city_h) / 2);
        canvas_glass.setFont(&fonts::Font0);
        canvas_glass.drawString(timeStr, tx + city_w + 2, ty + (max_h - time_h) / 2);
        
        if (p2d.depth > 0) {
            if (strcmp(city.name, "London") == 0) canvas_glass.fillCircle(p2d.x, p2d.y, 2, WHITE);
            else if (strcmp(city.name, "NewYork") == 0) canvas_glass.drawCircle(p2d.x, p2d.y, 2, WHITE);
            else if (strcmp(city.name, "Tokyo") == 0) canvas_glass.drawRect(p2d.x-1, p2d.y-1, 3, 3, WHITE);
        }
    }
    canvas_glass.pushSprite(0, 0);
}

// =======================================================================
// --- ディスプレイ2 (円形LCD) 関連の関数群 ---
// =======================================================================
void update_shadow_mask_lcd(const struct tm* utc_tm) {
    shadow_mask_lcd->fillSprite(0);
    float day = utc_tm->tm_yday + 1;
    float dec = -23.44f * cosf(2.0f * M_PI * (day + 10.0f) / 365.25f) * M_PI / 180.0f;
    float hf = utc_tm->tm_hour + utc_tm->tm_min / 60.0f;
    float sub_lon = (hf - 12.0f) * 15.0f * M_PI / 180.0f;
    const float R = 119.5f;
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 240; ++x) {
            float rx = x - R, ry = y - R, r = sqrtf(rx * rx + ry * ry);
            if (r > R) continue;
            float lat = asinf(std::max(-1.0f, std::min(1.0f, 1.0f - 2.0f * powf(r / R, 2.0f))));
            float lon = atan2f(rx, -ry) - M_PI;
            if ((sinf(lat) * sinf(dec) + cosf(lat) * cosf(dec) * cosf(lon - sub_lon)) < 0) {
                shadow_mask_lcd->drawPixel(x, y, 1);
            }
        }
    }
}
void draw_world_on_lcd(const struct tm* utc_time) {
    update_shadow_mask_lcd(utc_time);
    File f = SPIFFS.open("/map.jpg", "r");
    if (f) {
        sprite_lcd->drawJpg(&f, 0, 0);
        f.close();
    }
    uint16_t shadow_color = display_lcd.color888(0, 0, 50);
    sprite_lcd->startWrite();
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 240; ++x) {
            if (shadow_mask_lcd->readPixel(x, y)) {
                if ((x + y) % 2 == 0) sprite_lcd->drawPixel(x, y, shadow_color);
            }
        }
    }
    sprite_lcd->endWrite();
    float hf = utc_time->tm_hour + utc_time->tm_min / 60.0f;
    sprite_lcd->pushRotateZoom(120, 120, hf * -15.0f, 1.0, 1.0);
}

// =======================================================================
// --- LED関連の関数 (共通) ---
// =======================================================================
void updateLedPosition(const struct tm* utc_time) {
    int day = utc_time->tm_yday;
    double angle = ((double)day - 172.0) / 365.25 * 2.0 * PI;
    int startIdx = round(((cos(angle) + 1.0) / 2.0) * 12.0);
    pixels.clear();
    for (int i = 0; i < 3; i++) {
        pixels.setPixelColor(startIdx + i, pixels.Color(ledR, ledG, ledB));
    }
    pixels.show();
}

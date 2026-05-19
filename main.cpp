#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <ctime>
#include <cstdint>
#include <wincrypt.h>
#include <cmath>
#include "IconsFontAwesome6Brands.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_internal.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <windowsx.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "user32.lib")

static ID3D11Device*            g_pd3dDevice            = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext     = nullptr;
static IDXGISwapChain*          g_pSwapChain            = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView  = nullptr;

static std::string              tokenInput;
static std::string              statusText              = "Ready.";
static ID3D11ShaderResourceView* logoTexture            = nullptr;
static int                      logoW = 0, logoH = 0;
static ImFont* boldFont = nullptr;
void CreateRenderTarget();

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#define COL32_BLUE      IM_COL32(72,  185, 255, 255)
#define COL32_BLUE_40   IM_COL32(72,  185, 255, 40)
#define COL32_BLUE_80   IM_COL32(72,  185, 255, 80)
#define COL32_BLUE_120  IM_COL32(72,  185, 255, 120)
#define COL32_BG        IM_COL32(5,   10,  18,  255)
#define COL32_PANEL     IM_COL32(10,  20,  35,  255)
#define COL32_PANEL2    IM_COL32(8,   16,  28,  255)
#define COL32_WHITE     IM_COL32(255, 255, 255, 255)
#define COL32_MUTED     IM_COL32(130, 175, 210, 200)
#define COL32_GLOW      IM_COL32(72,  185, 255, 12)

static const ImVec4 V4_BLUE    = ImVec4(0.282f, 0.725f, 1.000f, 1.0f);
static const ImVec4 V4_WHITE   = ImVec4(1.000f, 1.000f, 1.000f, 1.0f);
static const ImVec4 V4_MUTED   = ImVec4(0.510f, 0.686f, 0.824f, 0.78f);
static const ImVec4 V4_DARK    = ImVec4(0.020f, 0.078f, 0.137f, 1.0f);
static const ImVec4 V4_TRANSP  = ImVec4(0.0f,   0.0f,   0.0f,   0.0f);
static HWND g_hWnd = nullptr;

static std::string trim(std::string s);
static std::string base64url_decode(const std::string& input);
static uint32_t crc32_raw(const std::string& data);
static std::string compute_crc32_str(const std::string& data);
static std::string current_timestamp();
static std::string steam_encrypt(const std::string& token, const std::string& account_name);
static std::pair<std::string,std::string> parse_clipboard(const std::string& input);
static std::string extract_steamid_from_jwt(const std::string& jwt);
static std::string read_file(const std::string& path);
static void write_file(const std::string& path, const std::string& content);
std::string get_steam_path();
std::string get_local_app_data();
static void write_autologin_user(const std::string& account_name);
static void check_steam_config_files(const std::string& config_dir);
static void inject_account_into_config(const std::string& path, const std::string& username, const std::string& steamid);
static std::string update_existing_user(const std::string& content, const std::string& username, const std::string& steamid);
static std::string insert_new_user(const std::string& content, const std::string& username, const std::string& steamid);
static void update_loginusers_vdf(const std::string& path, const std::string& username, const std::string& steamid);
static std::string create_new_local_vdf(const std::string& crc, const std::string& encrypted);
static std::string inject_connect_cache(const std::string& content, const std::string& crc, const std::string& encrypted);
static void write_local_vdf(const std::string& username, const std::string& token);
static std::string steamid64_to_steamid3(const std::string& steamid64);
static void write_localconfig_vdf(const std::string& steamid64);
void kill_steam_process();
static void delete_steam_files_and_folder(const std::string& config_dir, const std::string& steam_base_dir);
std::string clear_steam();
std::string handle_login(const std::string& tokenInput);
std::string get_clipboard_text();
bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height);

static std::string trim(std::string s)
{
    const char* ws = " \t\r\n";
    s.erase(0, s.find_first_not_of(ws));
    s.erase(s.find_last_not_of(ws) + 1);
    return s;
}

static std::string base64url_decode(const std::string& input)
{
    std::string b64 = input;
    for (char& c : b64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (b64.size() % 4) b64 += '=';
    static const char* CHARS =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int val = 0, bits = -8;
    for (unsigned char c : b64) {
        if (c == '=') break;
        const char* p = strchr(CHARS, c);
        if (!p) break;
        val = (val << 6) + (int)(p - CHARS);
        bits += 6;
        if (bits >= 0) {
            result.push_back((char)((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return result;
}

static uint32_t crc32_raw(const std::string& data)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char b : data) {
        crc ^= b;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

static std::string compute_crc32_str(const std::string& data)
{
    char hex[16];
    sprintf_s(hex, "%08x", crc32_raw(data));
    std::string s = hex;
    size_t start = s.find_first_not_of('0');
    if (start == std::string::npos) return "01";
    return s.substr(start) + "1";
}

static std::string current_timestamp()
{
    return std::to_string((uint64_t)std::time(nullptr));
}

static std::string steam_encrypt(const std::string& token, const std::string& account_name)
{
    DATA_BLOB dataIn  = { (DWORD)token.size(),        (BYTE*)token.c_str()        };
    DATA_BLOB entropy = { (DWORD)account_name.size(), (BYTE*)account_name.c_str() };
    DATA_BLOB dataOut = {};
    const wchar_t* desc = L"ObfuscateBuffer";
    if (!CryptProtectData(&dataIn, desc, &entropy, nullptr, nullptr, 0x11, &dataOut))
        throw std::runtime_error("CryptProtectData failed");
    std::string hex;
    hex.reserve(dataOut.cbData * 2);
    for (DWORD i = 0; i < dataOut.cbData; i++) {
        char buf[3];
        sprintf_s(buf, "%02x", dataOut.pbData[i]);
        hex += buf;
    }
    LocalFree(dataOut.pbData);
    return hex;
}

static std::pair<std::string, std::string> parse_clipboard(const std::string& input)
{
    std::string trimmed = trim(input);
    size_t pos = trimmed.find("----");
    if (pos == std::string::npos)
        throw std::runtime_error("Missing separator '----' in clipboard. Format: username----token");

    std::string username = trim(trimmed.substr(0, pos));
    std::string token = trim(trimmed.substr(pos + 4));

    if (username.empty() || token.empty())
        throw std::runtime_error("Username or token is empty");

    return {username, token};
}

static std::string extract_steamid_from_jwt(const std::string& jwt)
{
    size_t p1 = jwt.find('.');
    if (p1 == std::string::npos) throw std::runtime_error("Invalid JWT format");
    size_t p2 = jwt.find('.', p1 + 1);
    if (p2 == std::string::npos) throw std::runtime_error("Invalid JWT format");

    std::string payload_b64 = jwt.substr(p1 + 1, p2 - p1 - 1);
    std::string payload = base64url_decode(payload_b64);

    size_t sub_pos = payload.find("\"sub\"");
    if (sub_pos == std::string::npos) throw std::runtime_error("JWT missing 'sub' field");

    size_t colon = payload.find(':', sub_pos);
    if (colon == std::string::npos) throw std::runtime_error("JWT sub value malformed");

    size_t q1 = payload.find('"', colon);
    if (q1 == std::string::npos) throw std::runtime_error("JWT sub not a string");
    size_t q2 = payload.find('"', q1 + 1);
    if (q2 == std::string::npos) throw std::runtime_error("JWT sub string unterminated");

    return payload.substr(q1 + 1, q2 - q1 - 1);
}


static std::string read_file(const std::string& path)
{
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Failed to read file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void write_file(const std::string& path, const std::string& content)
{
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Failed to write file: " + path);
    f << content;
}

std::string get_steam_path() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Valve\\Steam", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        throw std::runtime_error("Steam registry key not found.");
    char path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    RegQueryValueExA(hKey, "SteamPath", nullptr, nullptr, (LPBYTE)path, &size);
    RegCloseKey(hKey);
    return path;
}

std::string get_local_app_data() {
    char path[MAX_PATH]{};
    SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path);
    return path;
}

static void write_autologin_user(const std::string& account_name)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Valve\\Steam", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        throw std::runtime_error("Failed to open Steam registry key for writing");
    LSTATUS st = RegSetValueExA(hKey, "AutoLoginUser", 0, REG_SZ,
        (const BYTE*)account_name.c_str(), (DWORD)(account_name.size() + 1));
    RegCloseKey(hKey);
    if (st != ERROR_SUCCESS)
        throw std::runtime_error("Failed to set AutoLoginUser registry value");
}

static void check_steam_config_files(const std::string& config_dir)
{
    if (!std::filesystem::exists(config_dir + "\\config.vdf") ||
        !std::filesystem::exists(config_dir + "\\loginusers.vdf"))
        throw std::runtime_error(
            "Please open Steam and log into any account first "
            "to create the necessary config files.");
}

static void inject_account_into_config(const std::string& path,
                                       const std::string& username,
                                       const std::string& steamid)
{
    std::string content = read_file(path);
    if (content.find("\"SteamID\"\t\t\"" + steamid + "\"") != std::string::npos)
        throw std::runtime_error("SteamID already exists in config.vdf");
    std::string block =
        "\n\t\t\t\t\t\"" + username + "\"\n"
        "\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\"SteamID\"\t\t\"" + steamid + "\"\n"
        "\t\t\t\t\t}\n";
    size_t pos = content.rfind("\"Accounts\"");
    if (pos == std::string::npos) throw std::runtime_error("Accounts block not found in config.vdf");
    size_t brace = content.find('{', pos);
    if (brace == std::string::npos) throw std::runtime_error("Accounts block malformed in config.vdf");
    content.insert(brace + 1, block);
    write_file(path, content);
}

static std::string update_existing_user(const std::string& content,
                                        const std::string& username,
                                        const std::string& steamid)
{
    std::string result;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        result += line + "\n";
        if (line.find("\"" + steamid + "\"") != std::string::npos) {
            std::string inner;
            while (std::getline(iss, inner)) {
                if      (inner.find("\"AccountName\"")  != std::string::npos)
                    result += "\t\t\t\"AccountName\"\t\t\"" + username + "\"\n";
                else if (inner.find("\"PersonaName\"")  != std::string::npos)
                    result += "\t\t\t\"PersonaName\"\t\t\"" + username + "\"\n";
                else if (inner.find("\"MostRecent\"")   != std::string::npos)
                    result += "\t\t\t\"MostRecent\"\t\t\"1\"\n";
                else if (inner.find("\"Timestamp\"")    != std::string::npos)
                    result += "\t\t\t\"Timestamp\"\t\t\"" + current_timestamp() + "\"\n";
                else
                    result += inner + "\n";
                if (trim(inner) == "}") break;
            }
        }
    }
    return result;
}

static std::string insert_new_user(const std::string& content,
                                   const std::string& username,
                                   const std::string& steamid)
{
    std::string block =
        "\n\t\"" + steamid + "\"\n"
        "\t{\n"
        "\t\t\"AccountName\"\t\t\""          + username            + "\"\n"
        "\t\t\"PersonaName\"\t\t\""          + username            + "\"\n"
        "\t\t\"RememberPassword\"\t\t\"1\"\n"
        "\t\t\"WantsOfflineMode\"\t\t\"0\"\n"
        "\t\t\"SkipOfflineModeWarning\"\t\t\"0\"\n"
        "\t\t\"AllowAutoLogin\"\t\t\"1\"\n"
        "\t\t\"MostRecent\"\t\t\"1\"\n"
        "\t\t\"Timestamp\"\t\t\""            + current_timestamp() + "\"\n"
        "\t}\n";
    size_t pos = content.rfind('}');
    if (pos == std::string::npos)
        throw std::runtime_error("Invalid loginusers.vdf format");
    std::string result = content;
    result.insert(pos, block);
    return result;
}

static void update_loginusers_vdf(const std::string& path,
                                  const std::string& username,
                                  const std::string& steamid)
{
    std::string content = read_file(path);
    std::string search  = "\"MostRecent\"\t\t\"1\"";
    std::string replace = "\"MostRecent\"\t\t\"0\"";
    for (size_t p = 0; (p = content.find(search, p)) != std::string::npos; )
        content.replace(p, search.size(), replace), p += replace.size();
    if (content.find("\"" + steamid + "\"") != std::string::npos)
        content = update_existing_user(content, username, steamid);
    else
        content = insert_new_user(content, username, steamid);
    write_file(path, content);
}

static std::string create_new_local_vdf(const std::string& crc, const std::string& encrypted)
{
    return
        "\"MachineUserConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n"
        "\t\t\t\"Steam\"\n\t\t\t{\n\t\t\t\t\"ConnectCache\"\n\t\t\t\t{\n"
        "\t\t\t\t\t\"" + crc + "\"\t\t\"" + encrypted + "\"\n"
        "\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n";
}

static std::string inject_connect_cache(const std::string& content,
                                        const std::string& crc,
                                        const std::string& encrypted)
{
    std::string output;
    std::istringstream iss(content);
    std::string line;
    bool in_cache = false;
    int  depth    = 0;
    bool replaced = false;
    while (std::getline(iss, line)) {
        std::string t = trim(line);
        if (t == "\"ConnectCache\"") {
            in_cache = true; depth = 0;
            output += line + "\n"; continue;
        }
        if (in_cache) {
            if (!t.empty() && t[0] == '{') depth++;
            else if (!t.empty() && t[0] == '}') {
                depth--;
                if (depth == 0 && !replaced) {
                    output += "\t\t\t\t\t\"" + crc + "\"\t\t\"" + encrypted + "\"\n";
                    replaced = true;
                }
            }
            if (!replaced && t.rfind("\"" + crc + "\"", 0) == 0) {
                output += "\t\t\t\t\t\"" + crc + "\"\t\t\"" + encrypted + "\"\n";
                replaced = true; continue;
            }
        }
        output += line + "\n";
    }
    if (!replaced) throw std::runtime_error("ConnectCache block not found in local.vdf");
    return output;
}

static void write_local_vdf(const std::string& username, const std::string& token)
{
    std::string crc       = compute_crc32_str(username);
    std::string encrypted = steam_encrypt(token, username);
    std::string base_path = get_local_app_data() + "\\Steam";
    std::string vdf_path  = base_path + "\\local.vdf";
    std::filesystem::create_directories(base_path);
    std::string content = std::filesystem::exists(vdf_path)
        ? inject_connect_cache(read_file(vdf_path), crc, encrypted)
        : create_new_local_vdf(crc, encrypted);
    write_file(vdf_path, content);
}

static std::string steamid64_to_steamid3(const std::string& steamid64)
{
    uint64_t id64 = std::stoull(steamid64);
    if (id64 < 76561197960265728ULL) throw std::runtime_error("SteamID64 out of range");
    return std::to_string(id64 - 76561197960265728ULL);
}

static void write_localconfig_vdf(const std::string& steamid64)
{
    std::string sid3 = steamid64_to_steamid3(steamid64);
    std::string content =
        "\"UserLocalConfigStore\"\n{\n\t\"friends\"\n\t{\n\t\t\"SignIntoFriends\" \"1\"\n\t}\n"
        "\t\"WebStorage\"\n\t{\n\t\t\"FriendStoreLocalPrefs_" + sid3 + "\""
        " \"{\\\"ePersonaState\\\":7,\\\"strNonFriendsAllowedToMsg\\\":\\\"\\\"}\"\n\t}\n}\n";
    std::string path = get_steam_path() + "\\userdata\\" + sid3 + "\\config\\localconfig.vdf";
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    write_file(path, content);
}

void kill_steam_process()
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Valve\\Steam\\ActiveProcess", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD pid = 0;
        DWORD size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "pid", nullptr, nullptr, (LPBYTE)&pid, &size) == ERROR_SUCCESS && pid != 0)
        {
            char cmd[64];
            sprintf_s(cmd, "taskkill /F /PID %lu /T >nul 2>&1", pid);
            system(cmd);
            Sleep(1200);
        }
        RegCloseKey(hKey);
    }
    system("taskkill /F /IM steam.exe /T >nul 2>&1");
    Sleep(800);
}

static void delete_steam_files_and_folder(const std::string& config_dir,
                                          const std::string& steam_base_dir)
{
    std::error_code ec;
    std::filesystem::remove(config_dir + "\\config.vdf",     ec);
    std::filesystem::remove(config_dir + "\\loginusers.vdf", ec);
    if (std::filesystem::exists(steam_base_dir))
        std::filesystem::remove_all(steam_base_dir, ec);
}

std::string clear_steam() {
    std::string steam_path = get_steam_path();
    std::string config_dir = steam_path + "\\config";
    std::string base_path  = get_local_app_data() + "\\Steam";
    kill_steam_process();
    delete_steam_files_and_folder(config_dir, base_path);
    kill_steam_process();
    delete_steam_files_and_folder(config_dir, base_path);
    return "Cleared Steam.";
}

std::string handle_login(const std::string& tokenIn)
{
    if (tokenIn.empty()) return "Paste a token first.";

    try
    {
        auto [username, jwt] = parse_clipboard(tokenIn);
        std::string steamid = extract_steamid_from_jwt(jwt);

        std::string steam_path = get_steam_path();
        std::string config_dir = steam_path + "\\config";

        check_steam_config_files(config_dir);

        kill_steam_process();

        inject_account_into_config(config_dir + "\\config.vdf", username, steamid);
        update_loginusers_vdf(config_dir + "\\loginusers.vdf", username, steamid);
        write_local_vdf(username, jwt);
        write_localconfig_vdf(steamid);
        write_autologin_user(username);

        return "Account '" + username + "' added successfully!\nOpen Steam.";
    }
    catch (const std::exception& e)
    {
        return std::string("Error: ") + e.what();
    }
}

std::string get_clipboard_text() {
    if (!OpenClipboard(nullptr)) return "";
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) { CloseClipboard(); return ""; }
    char* text = (char*)GlobalLock(hData);
    std::string result = text ? text : "";
    GlobalUnlock(hData);
    CloseClipboard();
    return result;
}

bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv,
                         int* out_width, int* out_height)
{
    int w = 0, h = 0;
    unsigned char* data = stbi_load(filename, &w, &h, nullptr, 4);
    if (!data) return false;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = data; sub.SysMemPitch = w * 4;
    ID3D11Texture2D* pTex = nullptr;
    g_pd3dDevice->CreateTexture2D(&desc, &sub, &pTex);
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    g_pd3dDevice->CreateShaderResourceView(pTex, &srvDesc, out_srv);
    pTex->Release();
    *out_width = w; *out_height = h;
    stbi_image_free(data);
    return true;
}
static void DrawGlowRect(ImDrawList* dl, ImVec2 a, ImVec2 b,
                         float rounding, ImU32 fill, ImU32 border, float borderW = 1.5f)
{
    dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1),
                IM_COL32(72, 185, 255, 25), rounding + 1, 0, borderW + 3.0f);
    dl->AddRectFilled(a, b, fill, rounding);
    dl->AddRect(a, b, border, rounding, 0, borderW);
}

static void DrawBeam(ImDrawList* dl, float x0, float x1, float y)
{
    dl->AddRectFilledMultiColor(
        ImVec2(x0, y - 1), ImVec2(x1, y + 1),
        IM_COL32(0,0,0,0), COL32_BLUE_120, COL32_BLUE_120, IM_COL32(0,0,0,0));
    float cx = (x0 + x1) * 0.5f;
    dl->AddCircleFilled(ImVec2(cx, y), 3.0f, COL32_BLUE);
    dl->AddCircleFilled(ImVec2(cx, y), 6.0f, COL32_BLUE_40);
}

static void DrawHexPattern(ImDrawList* dl, ImVec2 origin, ImU32 col)
{
    const float R  = 24.0f;
    const float H  = R * 1.7320508f;
    const float dx = R * 3.0f;
    for (int row = -3; row <= 3; row++) {
        for (int ci = -3; ci <= 3; ci++) {
            float cx = origin.x + ci * dx;
            float cy = origin.y + row * H + ((ci & 1) ? H * 0.5f : 0.0f);
            float dist = sqrtf((cx-origin.x)*(cx-origin.x) + (cy-origin.y)*(cy-origin.y));
            if (dist > 180.0f) continue;
            float fade = 1.0f - dist / 180.0f;
            ImU32 c = (col & 0x00FFFFFFu) | (ImU32)((float)((col >> 24) & 0xFF) * fade) << 24;
            ImVec2 pts[6];
            for (int i = 0; i < 6; i++) {
                float a = i * 60.0f * 3.14159265f / 180.0f;
                pts[i] = {cx + R * cosf(a), cy + R * sinf(a)};
            }
            dl->AddPolyline(pts, 6, c, ImDrawFlags_Closed, 1.0f);
        }
    }
}

static void DrawSpinner(ImDrawList* dl, ImVec2 centre, float radius, ImU32 col)
{
    const int segs = 9;
    for (int s = 0; s < segs; s++) {
        float a1 = (float)s       / segs * 2.0f * 3.14159265f - 3.14159265f * 0.5f;
        float a2 = (float)(s + 1) / segs * 2.0f * 3.14159265f - 3.14159265f * 0.5f - 0.15f;
        int   al = (int)(255.0f * (float)(s + 1) / segs);
        ImU32 sc = (col & 0x00FFFFFFu) | (ImU32)al << 24;
        dl->PathArcTo(centre, radius, a1, a2, 10);
        dl->PathStroke(sc, 0, 3.0f);
    }
}

static void DrawLockIcon(ImDrawList* dl, ImVec2 tl, float sz, ImU32 col)
{
    float hw = sz * 0.5f;
    float cx = tl.x + hw;
    dl->PathArcTo({cx, tl.y + hw * 0.85f}, hw * 0.65f, 3.14159265f, 0.0f, 20);
    dl->PathStroke(col, 0, sz * 0.12f);
    float by = tl.y + hw * 0.7f;
    dl->AddRectFilled({tl.x, by}, {tl.x + sz, tl.y + sz}, col, sz * 0.15f);
    dl->AddCircleFilled({cx, by + (sz - by - tl.y) * 0.4f + by - by},
                         sz * 0.1f, COL32_BG);
    float kcy = by + (tl.y + sz - by) * 0.38f;
    dl->AddCircleFilled({cx, kcy}, sz * 0.11f, COL32_BG);
}

static void DrawClipboard(ImDrawList* dl, ImVec2 tl, float sz, ImU32 col)
{
    float bw = sz * 0.12f;
    float clip_w = sz * 0.4f, clip_h = sz * 0.22f;
    float clip_x = tl.x + sz * 0.5f - clip_w * 0.5f;
    dl->AddRect({tl.x, tl.y + clip_h * 0.5f}, {tl.x + sz, tl.y + sz}, col, 2.0f, 0, bw);
    dl->AddRectFilled({clip_x, tl.y}, {clip_x + clip_w, tl.y + clip_h + 1},
                      COL32_PANEL2);
    dl->AddRect({clip_x, tl.y}, {clip_x + clip_w, tl.y + clip_h}, col, 1.0f, 0, bw);
}
static float HoverAnim(const char* id, bool hovered, float speed = 10.0f)
{
    ImGuiID key = ImGui::GetID(id);
    ImGuiStorage* storage = ImGui::GetStateStorage();

    float v = storage->GetFloat(key, 0.0f);
    float target = hovered ? 1.0f : 0.0f;

    v = ImLerp(v, target, ImClamp(ImGui::GetIO().DeltaTime * speed, 0.0f, 1.0f));
    storage->SetFloat(key, v);

    return v;
}
static bool CustomButton(ImDrawList* dl, const char* label,
                         ImVec2 tl, ImVec2 br,
                         float rounding,
                         ImU32 fillNormal, ImU32 fillHover, ImU32 fillActive,
                         ImU32 borderCol, float borderW,
                         ImU32 textCol)
{
    ImVec2 size = ImVec2(br.x - tl.x, br.y - tl.y);

    ImGui::SetCursorScreenPos(tl);
    ImGui::PushStyleColor(ImGuiCol_Button,        V4_TRANSP);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  V4_TRANSP);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   V4_TRANSP);
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding, rounding);
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameBorderSize, 0.0f);
    bool clicked = ImGui::Button(("##btn_" + std::string(label)).c_str(), size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    bool hovered = ImGui::IsItemHovered();
bool active  = ImGui::IsItemActive();

float anim = HoverAnim(label, hovered);
float press = active ? 2.0f : 0.0f;

ImVec2 atl = ImVec2(tl.x, tl.y - anim * 2.0f + press);
ImVec2 abr = ImVec2(br.x, br.y - anim * 2.0f + press);

ImU32 fill = active
    ? fillActive
    : ImGui::GetColorU32(ImLerp(
        ImGui::ColorConvertU32ToFloat4(fillNormal),
        ImGui::ColorConvertU32ToFloat4(fillHover),
        anim
    ));

int glowAlpha = (int)(12 + anim * 45);

dl->AddRectFilled(
    ImVec2(atl.x - 3 - anim * 3, atl.y - 3 - anim * 3),
    ImVec2(abr.x + 3 + anim * 3, abr.y + 3 + anim * 3),
    IM_COL32(72,185,255, glowAlpha),
    rounding + 4
);

dl->AddRectFilled(atl, abr, fill, rounding);
dl->AddRect(atl, abr, COL32_BLUE, rounding, 0, borderW + anim * 1.3f);

    ImVec2 ts   = ImGui::CalcTextSize(label);
    ImVec2 tpos = ImVec2(atl.x + (size.x - ts.x) * 0.5f,
                     atl.y + (size.y - ts.y) * 0.5f);
    dl->AddText(tpos, textCol, label);

    return clicked;
}

static bool IconButton(ImDrawList* dl, const char* icon, const char* label,
                       ImVec2 tl, ImVec2 br, float rounding,
                       ImU32 fillN, ImU32 fillH, ImU32 fillA, ImU32 border, float bw)
{
    ImVec2 size = ImVec2(br.x - tl.x, br.y - tl.y);
    ImGui::SetCursorScreenPos(tl);
    ImGui::PushStyleColor(ImGuiCol_Button,        V4_TRANSP);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  V4_TRANSP);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   V4_TRANSP);
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding, rounding);
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameBorderSize, 0.0f);
    bool clicked = ImGui::Button(("##ibtn_" + std::string(label)).c_str(), size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();
    float anim = HoverAnim(label, hovered);
float press = active ? 2.0f : 0.0f;

tl.y -= anim * 2.0f;
br.y -= anim * 2.0f;

tl.y += press;
br.y += press;
    ImU32 fill   = active ? fillA : (hovered ? fillH : fillN);

    dl->AddRectFilled(
    ImVec2(tl.x - 3 - anim * 3, tl.y - 3 - anim * 3),
    ImVec2(br.x + 3 + anim * 3, br.y + 3 + anim * 3),
    IM_COL32(72,185,255, (int)(10 + anim * 50)),
    rounding + 4
);

    dl->AddRectFilled(tl, br, fill, rounding);
    dl->AddRect(tl, br, COL32_BLUE, rounding, 0, bw + anim * 1.2f);

    ImGui::PushFont(nullptr);
    ImVec2 iconSz = ImGui::CalcTextSize(icon);
    ImVec2 lblSz  = ImGui::CalcTextSize(label);
    float  totalW = iconSz.x + 10 + lblSz.x;
    float  startX = tl.x + (size.x - totalW) * 0.5f;
    float  midY   = tl.y + (size.y - iconSz.y) * 0.5f;
    dl->AddText(ImVec2(startX, midY), COL32_BLUE, icon);
    dl->AddText(ImVec2(startX + iconSz.x + 10, midY), COL32_BLUE, label);
    ImGui::PopFont();

    return clicked;
}

void DrawUI()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
const float W = display.x;
const float H = display.y;
    static char tokenBuffer[4096] = "";

    ImGui::SetNextWindowPos(ImVec2(0, 0));
ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGui::PushStyleColor(ImGuiCol_WindowBg,     V4_TRANSP);
    ImGui::PushStyleColor(ImGuiCol_Border,        V4_TRANSP);
    ImGui::PushStyleVar  (ImGuiStyleVar_WindowPadding,    ImVec2(0,0));
    ImGui::PushStyleVar  (ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar  (ImGuiStyleVar_ItemSpacing,      ImVec2(0,0));

    ImGui::Begin("##Root", nullptr,
          ImGuiWindowFlags_NoTitleBar |
    ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoScrollbar |
    ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2      wp = ImGui::GetWindowPos();
float btnSize = 24.0f;
float winBtnGap = 8.0f;
float topY = wp.y + 14.0f;

ImVec2 closeTL = { wp.x + W - 14.0f - btnSize, topY };
ImVec2 minTL   = { closeTL.x - winBtnGap - btnSize, topY };

    dl->AddRectFilled({wp.x, wp.y}, {wp.x+W, wp.y+H}, COL32_BG, 14.0f);

    dl->AddRectFilledMultiColor(
        {wp.x, wp.y}, {wp.x+W, wp.y+260},
        IM_COL32(18,48,90,80), IM_COL32(18,48,90,80),
        IM_COL32(5,10,18,0),   IM_COL32(5,10,18,0));

    dl->AddRect({wp.x+1,wp.y+1}, {wp.x+W-1,wp.y+H-1}, COL32_BLUE_80, 14.0f, 0, 1.5f);

    const float cr = 14.0f;
    dl->AddCircleFilled({wp.x+cr,   wp.y+cr  }, 3.0f, COL32_BLUE);
    dl->AddCircleFilled({wp.x+W-cr, wp.y+cr  }, 3.0f, COL32_BLUE);
    dl->AddCircleFilled({wp.x+cr,   wp.y+H-cr}, 3.0f, COL32_BLUE_80);
    dl->AddCircleFilled({wp.x+W-cr, wp.y+H-cr}, 3.0f, COL32_BLUE_80);

    DrawHexPattern(dl, {wp.x + 110, wp.y + 100}, IM_COL32(72,185,255,22));
    DrawHexPattern(dl, {wp.x+W-110, wp.y + 100}, IM_COL32(72,185,255,22));

    const float logoSz = 118.0f;
    const float logoCX = wp.x + W * 0.5f;
    const float logoCY = wp.y + 65.0f + logoSz * 0.5f;

    dl->AddCircleFilled({logoCX, logoCY}, 80.0f, IM_COL32(72,185,255,10));
    dl->AddCircleFilled({logoCX, logoCY}, 55.0f, IM_COL32(72,185,255,12));
    dl->AddCircle({logoCX, logoCY}, logoSz * 0.58f, IM_COL32(72,185,255,35), 64, 1.2f);

    if (logoTexture) {
    float realLogoSize = logoSz * 1.75f;

    float logoOffsetX = 1.0f;
    float logoOffsetY = 05.0f; 

    ImGui::SetCursorScreenPos({
        logoCX - realLogoSize * 0.5f + logoOffsetX,
        logoCY - realLogoSize * 0.5f + logoOffsetY
    });

    ImGui::Image((ImTextureID)logoTexture, {realLogoSize, realLogoSize});
}
auto WindowBtn = [&](const char* id, ImVec2 tl, ImU32 col, const char* symbol) -> bool {
    ImGui::SetCursorScreenPos(tl);
    ImGui::InvisibleButton(id, {btnSize, btnSize});

    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();

    float a = HoverAnim(id, hov, 12.0f);
    ImVec2 br = { tl.x + btnSize, tl.y + btnSize };

    dl->AddRectFilled(
        tl, br,
        hov ? IM_COL32(255,255,255,25) : IM_COL32(255,255,255,8),
        6.0f
    );

    dl->AddRect(tl, br, col, 6.0f, 0, 1.0f + a);

    ImVec2 ts = ImGui::CalcTextSize(symbol);
    dl->AddText(
        { tl.x + (btnSize - ts.x) * 0.5f, tl.y + (btnSize - ts.y) * 0.5f - 1 },
        col,
        symbol
    );

    return ImGui::IsItemClicked();
};

if (WindowBtn("##min_btn", minTL, IM_COL32(255,210,70,255), "—"))
    ShowWindow(g_hWnd, SW_MINIMIZE);

if (WindowBtn("##close_btn", closeTL, IM_COL32(255,75,75,255), "X"))
    PostQuitMessage(0);
ImGui::PushFont(boldFont);
   const float titleScale = 4.0f;
ImGui::SetWindowFontScale(titleScale);

const char* titleA = "Jamals ";
const char* titleB = "Services";

ImVec2 szA = ImGui::CalcTextSize(titleA);
ImVec2 szB = ImGui::CalcTextSize(titleB);
ImVec2 titleSz = ImVec2(szA.x + szB.x, szA.y);
float titleX = wp.x + (W - (szA.x + szB.x)) * 0.5f + 10.0f;
float titleY = logoCY + logoSz * 0.5f + 12.0f;

float pulse = (sinf((float)ImGui::GetTime() * 2.2f) + 1.0f) * 0.5f;
int glowAlpha = (int)(45 + pulse * 45);

dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {titleX + 3, titleY + 3},
            IM_COL32(72,185,255, glowAlpha), titleA);
dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {titleX + szA.x + 3, titleY + 3},
            IM_COL32(255,255,255, glowAlpha), titleB);

dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {titleX, titleY},
            COL32_BLUE, titleA);
dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {titleX + szA.x, titleY},
            COL32_WHITE, titleB);

ImGui::SetWindowFontScale(1.0f);
ImGui::PopFont();
    float beamY = titleY + titleSz.y + 18.0f;
    DrawBeam(dl, wp.x + 40, wp.x + W - 40, beamY);
    float tokenTop = beamY + 22.0f;
    float tokenBot = tokenTop + 88.0f;
    ImVec2 tokenTL = {wp.x + 36, tokenTop};
    ImVec2 tokenBR = {wp.x + W - 36, tokenBot};
    DrawGlowRect(dl, tokenTL, tokenBR, 10.0f, COL32_PANEL, COL32_BLUE_80);

    {
        ImGui::SetWindowFontScale(1.1f);
        ImVec2 lsz = ImGui::CalcTextSize("ENTER YOUR TOKEN:");
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    {tokenTL.x + 18, tokenTL.y + 13}, COL32_WHITE, "ENTER YOUR TOKEN:");
        ImGui::SetWindowFontScale(1.0f);
    }

    ImVec2 inputTL = {tokenTL.x + 14, tokenTL.y + 40};
    ImVec2 inputBR = {tokenBR.x - 160, tokenBR.y - 12};
    DrawGlowRect(dl, inputTL, inputBR, 7.0f, COL32_PANEL2, COL32_BLUE_40, 1.0f);

    ImGui::SetCursorScreenPos({inputTL.x + 2, inputTL.y + 2});
    float inputW = inputBR.x - inputTL.x - 4;
    float inputH = inputBR.y - inputTL.y - 4;
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        V4_TRANSP);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, V4_TRANSP);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  V4_TRANSP);
    ImGui::PushStyleColor(ImGuiCol_Border,          V4_TRANSP);
    ImGui::PushStyleColor(ImGuiCol_Text,            V4_WHITE);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(10, (inputH - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::PushItemWidth(inputW);
    ImGui::InputTextWithHint("##tok", "Paste your token here...",
                             tokenBuffer, sizeof(tokenBuffer));
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);

    ImVec2 pasteTL = {inputBR.x + 10, inputTL.y};
    ImVec2 pasteBR = {tokenBR.x - 12, inputBR.y};
    bool pastePushed = CustomButton(dl, "    PASTE", pasteTL, pasteBR, 7.0f,
                       IM_COL32(10,40,80,255), IM_COL32(20,75,140,255),
                       IM_COL32(72,185,255,255),
                       COL32_BLUE, 1.5f, COL32_WHITE);
    float cbSz = 14.0f;
    DrawClipboard(dl, {pasteTL.x + 10, pasteTL.y + (pasteBR.y - pasteTL.y) * 0.5f - cbSz * 0.5f},
                  cbSz, COL32_WHITE);
    if (pastePushed) {
        std::string clip = get_clipboard_text();
        if (!clip.empty() && clip.size() < sizeof(tokenBuffer)) {
            strcpy_s(tokenBuffer, clip.c_str());
            statusText = "Token pasted.";
        } else {
            statusText = "Clipboard empty.";
        }
    }

    float loginTop = tokenBot + 18.0f;
    float loginBot = loginTop + 58.0f;
    ImVec2 loginTL = {wp.x + 120, loginTop};
    ImVec2 loginBR = {wp.x + W - 120, loginBot};
    ImVec2 loginSz = {loginBR.x - loginTL.x, loginBR.y - loginTL.y};

    ImGui::SetCursorScreenPos(loginTL);
    ImGui::PushStyleColor(ImGuiCol_Button,        V4_TRANSP);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  V4_TRANSP);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   V4_TRANSP);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,  0.0f);
    bool loginClicked = ImGui::Button("##login", loginSz);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    bool loginHov = ImGui::IsItemHovered();
    bool loginAct = ImGui::IsItemActive();
    float loginAnim = HoverAnim("login_button", loginHov, 9.0f);
float loginPress = loginAct ? 2.0f : 0.0f;

loginTL.y -= loginAnim * 3.0f;
loginBR.y -= loginAnim * 3.0f;

loginTL.y += loginPress;
loginBR.y += loginPress;

    dl->AddRectFilled(
    {loginTL.x - 4 - loginAnim * 5, loginTL.y - 4 - loginAnim * 5},
    {loginBR.x + 4 + loginAnim * 5, loginBR.y + 4 + loginAnim * 5},
    IM_COL32(72,185,255, (int)(18 + loginAnim * 60)),
    15.0f
);

    ImU32 lgL = loginAct ? IM_COL32(45,140,240,255) : (loginHov ? IM_COL32(35,120,210,255) : IM_COL32(22,90,185,255));
    ImU32 lgR = loginAct ? IM_COL32(105,215,255,255): (loginHov ? IM_COL32(90,205,255,255) : IM_COL32(72,185,255,255));
    dl->AddRectFilledMultiColor(loginTL, loginBR, lgL, lgR, lgR, lgL);
    dl->AddRect(loginTL, loginBR, COL32_BLUE, 10.0f, 0, 1.5f + loginAnim * 1.8f);

ImGui::SetWindowFontScale(1.55f);

const char* loginText = "LOGIN";
ImVec2 lblSz = ImGui::CalcTextSize(loginText);

float lblX = loginTL.x + ((loginBR.x - loginTL.x) - lblSz.x) * 0.5f;
float lblY = loginTL.y + ((loginBR.y - loginTL.y) - lblSz.y) * 0.5f;

dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
            {lblX, lblY}, COL32_WHITE, loginText);

ImGui::SetWindowFontScale(1.0f);

    if (loginClicked)
        statusText = handle_login(std::string(tokenBuffer));

    float cardsTop = loginBot + 20.0f;
    float cardsBot = cardsTop + 188.0f;
    float gap      = 14.0f;
    float cardsTotalW = W - 36.0f * 2 - gap * 2;
    float cW       = cardsTotalW / 3.0f;
    float cX[3]    = { wp.x + 36, wp.x + 36 + cW + gap, wp.x + 36 + (cW + gap) * 2 };

    struct CardDef { const char* title; const char* label; const char* sub; int type; };
    CardDef cards[3] = {
        { "BUY NFA'S HERE:",   "DISCORD",     "Join our Discord server!",     0 },
        { "VIEW SOURCECODE:",  "CODE",        "View source code on GitHub!",  1 },
        { "CLEAR STEAM:",      "CLEAR STEAM", "Clears your Steam data.",       2 },
    };

    for (int i = 0; i < 3; i++) {
        ImVec2 cTL = {cX[i],    cardsTop};
        ImVec2 cBR = {cX[i]+cW, cardsBot};

        ImGui::SetCursorScreenPos(cTL);
        ImGui::PushStyleColor(ImGuiCol_Button,        V4_TRANSP);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  V4_TRANSP);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   V4_TRANSP);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,  0.0f);
        bool cardClicked = ImGui::Button(("##card" + std::to_string(i)).c_str(),
                                         {cW, cardsBot - cardsTop});
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        bool cHov = ImGui::IsItemHovered();
        bool cAct = ImGui::IsItemActive();

        ImU32 cFill = cAct ? IM_COL32(15,40,75,255)
                    : cHov ? IM_COL32(12,32,62,255)
                    :        COL32_PANEL;
        DrawGlowRect(dl, cTL, cBR, 10.0f, cFill, cHov ? COL32_BLUE : COL32_BLUE_80);

        if (cHov)
            dl->AddRectFilled({cTL.x-2,cTL.y-2},{cBR.x+2,cBR.y+2},
                              IM_COL32(72,185,255,15), 12.0f);

        float cCX  = cTL.x + cW * 0.5f;
        float cCY  = cTL.y + (cardsBot - cardsTop) * 0.5f;

        ImGui::SetWindowFontScale(0.9f);
        ImVec2 tsz = ImGui::CalcTextSize(cards[i].title);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    {cCX - tsz.x * 0.5f, cTL.y + 12}, COL32_WHITE, cards[i].title);
        ImGui::SetWindowFontScale(1.0f);

        float iconY = cTL.y + 44.0f;
        float iconSz = 42.0f;
        if (cards[i].type == 0) {
            ImVec2 isz = ImGui::CalcTextSize(ICON_FA_DISCORD);
            dl->AddText(ImGui::GetFont(), iconSz,
                        {cCX - iconSz * 0.62f, iconY}, COL32_BLUE, ICON_FA_DISCORD);
        } else if (cards[i].type == 1) {
            dl->AddText(ImGui::GetFont(), iconSz,
                        {cCX - iconSz * 0.62f, iconY}, COL32_BLUE, ICON_FA_GITHUB);
        } else {
            DrawSpinner(dl, {cCX, iconY + iconSz * 0.5f}, iconSz * 0.5f, COL32_BLUE);
        }

        ImGui::SetWindowFontScale(1.25f);
        ImVec2 lsz2 = ImGui::CalcTextSize(cards[i].label);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    {cCX - lsz2.x * 0.5f, iconY + iconSz + 10},
                    COL32_BLUE, cards[i].label);
        ImGui::SetWindowFontScale(1.0f);

        ImVec2 ssz2 = ImGui::CalcTextSize(cards[i].sub);
        dl->AddText({cCX - ssz2.x * 0.5f, cBR.y - 28}, COL32_MUTED, cards[i].sub);

        if (cardClicked) {
            if      (cards[i].type == 0)
                ShellExecuteA(nullptr,"open","https://discord.gg/Zf8QTatmJM",nullptr,nullptr,SW_SHOWNORMAL);
            else if (cards[i].type == 1)
                ShellExecuteA(nullptr,"open","https://github.com/MonsterScripts/Jamal-s-Services/",nullptr,nullptr,SW_SHOWNORMAL);
            else {
    ImGui::OpenPopup("Clear Steam Confirm");
}
        }
    }

    {
        ImVec2 ssz = ImGui::CalcTextSize(statusText.c_str());
        dl->AddText({wp.x + (W - ssz.x) * 0.5f, cardsBot + 10},
                    COL32_BLUE, statusText.c_str());
    }
ImGui::SetNextWindowSize(ImVec2(430, 210), ImGuiCond_Appearing);
ImGui::SetNextWindowPos(
    ImVec2(wp.x + W * 0.5f, wp.y + H * 0.5f),
    ImGuiCond_Appearing,
    ImVec2(0.5f, 0.5f)
);

ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(7, 14, 24, 255));
ImGui::PushStyleColor(ImGuiCol_Border,   IM_COL32(72, 185, 255, 180));
ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 18));

if (ImGui::BeginPopupModal("Clear Steam Confirm", nullptr,
    ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoScrollbar))
{
    ImDrawList* pdl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 s = ImGui::GetWindowSize();

    pdl->AddRectFilled(p, ImVec2(p.x + s.x, p.y + 46),
        IM_COL32(10, 35, 60, 255), 14.0f,
        ImDrawFlags_RoundCornersTop);

    pdl->AddRect(p, ImVec2(p.x + s.x, p.y + s.y),
        IM_COL32(72,185,255,190), 14.0f, 0, 1.5f);

    ImGui::SetCursorPosY(10);
    ImGui::TextColored(V4_BLUE, "Clear Steam Confirmation");

    ImGui::SetCursorPosY(62);
    ImGui::TextWrapped(
        "This will sign you out of all Steam accounts saved on this device "
        "and clear your cache."
    );

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1, 1, 1, 0.9f), "Are you sure you want to continue?");

    ImGui::SetCursorPosY(158);

    if (ImGui::Button("Yes", ImVec2(185, 34)))
    {
        try { statusText = clear_steam(); }
        catch (const std::exception& e) { statusText = e.what(); }

        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(185, 34)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

ImGui::PopStyleVar(3);
ImGui::PopStyleColor(2);
    {
        const char* foot = "\xC2\xA9 2025 JAMALS SERVICES";
        ImVec2 fsz = ImGui::CalcTextSize(foot);
        float  fy  = wp.y + H - 24.0f;
        float  fx  = wp.x + (W - fsz.x) * 0.5f;

        dl->AddLine({wp.x + 80, fy + fsz.y * 0.5f}, {fx - 16, fy + fsz.y * 0.5f},
                    COL32_BLUE_40);
        dl->AddLine({fx + fsz.x + 16, fy + fsz.y * 0.5f}, {wp.x+W-80, fy + fsz.y * 0.5f},
                    COL32_BLUE_40);
        dl->AddText({fx, fy}, COL32_BLUE_80, foot);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fla[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, fla, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            flags, fla, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext);
    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)          { g_pSwapChain->Release();          g_pSwapChain          = nullptr; }
    if (g_pd3dDeviceContext)   { g_pd3dDeviceContext->Release();   g_pd3dDeviceContext   = nullptr; }
    if (g_pd3dDevice)          { g_pd3dDevice->Release();          g_pd3dDevice          = nullptr; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
        case WM_NCHITTEST:
{
    POINT p;
    p.x = GET_X_LPARAM(lParam);
    p.y = GET_Y_LPARAM(lParam);
    ScreenToClient(hWnd, &p);

    RECT rc;
    GetClientRect(hWnd, &rc);

    if (p.y >= 0 && p.y <= 60 && p.x >= rc.right - 120)
        return HTCLIENT;

    if (p.y >= 0 && p.y <= 45)
        return HTCAPTION;

    return HTCLIENT;
}
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        DestroyWindow(g_hWnd); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
        GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
        L"Jamals Services", nullptr };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(
    WS_EX_APPWINDOW,
    wc.lpszClassName,
    L"Jamals Services",
    WS_POPUP,
    100, 100, 1000, 760,
    nullptr, nullptr, wc.hInstance, nullptr
);
g_hWnd = hwnd;
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    HRGN region = CreateRoundRectRgn(
    0, 0,
    1000, 760,
    28, 28
);

SetWindowRgn(hwnd, region, TRUE);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    boldFont = io.Fonts->AddFontFromFileTTF(
    "C:\\Windows\\Fonts\\segoeuib.ttf",
    18.0f
);

static const ImWchar brand_ranges[] = { ICON_MIN_FAB, ICON_MAX_16_FAB, 0 };

ImFontConfig brand_config;
brand_config.MergeMode = true;
brand_config.PixelSnapH = true;

ImFont* brands = io.Fonts->AddFontFromFileTTF(
    "Font Awesome 7 Brands-Regular-400.otf",
    18.0f,
    &brand_config,
    brand_ranges
);

if (!brands)
{
    MessageBoxA(nullptr, "Could not load Font Awesome Brands font.", "Font error", MB_OK);
}
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    LoadTextureFromFile("logo.png", &logoTexture, &logoW, &logoH);

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawUI();

        ImGui::Render();
        const float cc[4] = { 0,0,0,0 };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    if (logoTexture) logoTexture->Release();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}
#define _CRT_SECURE_NO_WARNINGS

// ===== Win32 / System =====
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <tchar.h>
#include <commdlg.h>
#include <dbt.h>

// ===== C++ STL =====
#include <vector>
#include <string>
#include <fstream>
#include <thread>
#include <cstdio>
#include <cstdarg>
#include <algorithm>
#include <atomic>

// ===== ImGui =====
#include "../imgui/imgui.h"
#include "../imgui/backends/imgui_impl_win32.h"
#include "../imgui/backends/imgui_impl_dx11.h"

// ===== ZeroCrypto Core =====
#include "core/VaultRegistry.h"
#include "core/SystemUtils.h"
#include "core/SecureBuffer.h"
#include "Crypto.h" 

// Fix for Drag & Drop UIPI issue
#ifndef WM_COPYGLOBALDATA
#define WM_COPYGLOBALDATA 0x0049
#endif
#ifndef MSGFLT_ADD
#define MSGFLT_ADD 1
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Link necessary libraries 
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib") 
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

// ================== Global Data ==================
struct AppConfig {
    bool killOnUSB = true;
    bool autoUnmount = false;
    bool wipeOnPanic = false;
    int lastActiveIndex = 0; 
};

static const char* DEV_PORTFOLIO_URL = "https://prof-0.github.io/Zero//#contact";

// UI State Flags
// REMOVED g_OpenAddPopup to fix ghost build error
static bool g_ShowAddWindow = false; // "Add Vault" window
static bool g_ShowCreateWindow = false; // "Create Vault" window
static bool g_ShowCreationProgress = false; // Popup for async creation
static bool g_ShowSuccess = false; // Mount success
static bool g_ShowVaultCreatedSuccess = false; // Creation success modal
static bool g_focusPassword = false; // Trigger to focus password input
static bool g_OpenCreatePopup = false;

// Direct X
static ID3D11Device* g_Device = nullptr;
static ID3D11DeviceContext* g_Context = nullptr;
static IDXGISwapChain* g_SwapChain = nullptr;
static ID3D11RenderTargetView* g_RTV = nullptr;

// Data Buffers
static char newVaultName[64] = "";
static char newVaultPath[MAX_PATH] = "";
static int  driveIndex = 0;
static const char* driveLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static char createPath[MAX_PATH] = "";
static SecureBuffer g_createPassword(128);
static int  createSizeVal = 500; 
static int  createSizeUnit = 0; // 0 = MB, 1 = GB

static AppConfig g_config; 
static std::vector<std::string> g_logs; 
static bool g_switchToLogs = false;     
static char g_mountedDrive = 0;
static bool g_isMounting = false;

// Async Creation State
static std::atomic<bool> g_isCreating{false};
static std::atomic<bool> g_creationDone{false};
static std::atomic<bool> g_creationSuccess{false};
static int g_creationExitCode = 0;

static SecureBuffer g_vaultPassword(128);

#define HOTKEY_ID_PANIC 1001

// ================== Helper Functions ==================

void AddLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
    buf[IM_ARRAYSIZE(buf)-1] = 0;
    va_end(args);
    g_logs.push_back(std::string(buf));
}

void OpenDeveloperPortfolio() {
    ShellExecuteA(NULL, "open", DEV_PORTFOLIO_URL, NULL, NULL, SW_SHOWNORMAL);
}

// ================== Persistence ==================
std::string GetConfigPath() { return SystemUtils::GetBaseDir() + "\\config.bin"; }

void SaveConfig() {
    std::ofstream ofs(GetConfigPath(), std::ios::binary);
    if (ofs.is_open()) { ofs.write((char*)&g_config, sizeof(AppConfig)); ofs.close(); }
}

void LoadConfig() {
    std::ifstream ifs(GetConfigPath(), std::ios::binary);
    if (ifs.is_open()) { ifs.read((char*)&g_config, sizeof(AppConfig)); ifs.close(); }
}

void ResetConfig() {
    g_config.killOnUSB = true;
    g_config.autoUnmount = false;
    g_config.wipeOnPanic = false;
    g_config.lastActiveIndex = 0;
    SaveConfig();
    AddLog("[INFO] Settings reset to default.");
}

void SaveEncryptedLogs() {
    if (g_logs.empty()) { AddLog("[WARN] No logs to save."); return; }
    std::string fullLog = "";
    for (const auto& line : g_logs) fullLog += line + "\n";
    std::vector<uint8_t> rawData(fullLog.begin(), fullLog.end());
    std::vector<uint8_t> encryptedData = Encrypt(rawData); // Uses DPAPI
    std::ofstream outFile("system.log.enc", std::ios::binary);
    if (outFile.is_open()) {
        outFile.write(reinterpret_cast<const char*>(encryptedData.data()), encryptedData.size());
        outFile.close();
        AddLog("[SECURE] Logs encrypted & saved to 'system.log.enc'");
    } else {
        AddLog("[ERROR] Failed to save log file!");
    }
}

// ================== Styling ==================
void SetupProfessionalStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(8, 6);
    style.FramePadding = ImVec2(6, 4);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]       = ImVec4(0.06f, 0.06f, 0.06f, 0.98f);
    colors[ImGuiCol_PopupBg]        = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_Text]           = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_Border]         = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_Header]         = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_HeaderActive]   = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_Tab]            = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TabHovered]     = ImVec4(0.00f, 0.55f, 0.55f, 0.60f);
    colors[ImGuiCol_TabActive]      = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBg]        = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_Button]         = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.00f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.00f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_CheckMark]      = ImVec4(0.00f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.7f);
}

// ================== Core Logic ==================

void UnmountVault() {
    AddLog("[CMD] Executing Force Dismount...");
    std::string exeDir = SystemUtils::GetBaseDir();
    std::string vcPath = exeDir + "\\assets\\veracrypt\\VeraCrypt.exe";
    
    // Safety check: ensure binary exists unless in panic mode
    if (!SystemUtils::FileExists(vcPath)) {
         AddLog("[WARN] VeraCrypt not found, assuming dismount.");
         // Proceed to cleanup anyway
    } else {
        DWORD exitCode = SystemUtils::ExecuteProcess(vcPath, "/dismount /force /quit /silent", false);
        if (exitCode != 0) AddLog("[WARN] Dismount command failed with exit code %d.", exitCode);
    }
    
    // Auto-open portfolio on successful unmount (if not panic/wiping)
    if (!g_config.wipeOnPanic) {
        OpenDeveloperPortfolio();
    }

    if (g_config.wipeOnPanic) {
        AddLog("[PANIC] Wiping Configuration...");
        SystemUtils::SecureWipeFile(GetConfigPath());
        SystemUtils::SecureWipeFile("zerocrypto.cfg"); 
        AddLog("[PANIC] Terminating.");
        Sleep(500); 
        exit(0); 
    }
    AddLog("[CMD] Dismount command sent.");
}

void AttemptAutorun(char driveLetter) {
    std::string scriptPath = std::string(1, driveLetter) + ":\\SecureEnv\\StartEnv.bat";
    AddLog("[AUTORUN] Checking: %s", scriptPath.c_str());
    Sleep(1500); 
    if (SystemUtils::FileExists(scriptPath)) {
        AddLog("[AUTORUN] Found! Executing...");
        SystemUtils::ExecuteProcess("powershell", "-WindowStyle Hidden -Command \"Get-ChildItem -Path '" + std::string(1, driveLetter) + ":\\SecureEnv' -Recurse | Unblock-File\"", false);
        std::string workingDir = std::string(1, driveLetter) + ":\\SecureEnv";
        ShellExecuteA(NULL, "open", scriptPath.c_str(), NULL, workingDir.c_str(), SW_SHOWNORMAL);
        AddLog("[AUTORUN] Environment launched.");
    } else {
        AddLog("[AUTORUN] 'StartEnv.bat' not found.");
    }
}

// Background Worker for Vault Creation
void VaultCreationWorker(std::string path, std::string password, int sizeMB) {
    std::string fmtPath = SystemUtils::GetBaseDir() + "\\assets\\veracrypt\\VeraCrypt Format.exe";
    
    // Construct args
    std::string args = "/create \"" + path + 
                       "\" /size " + std::to_string(sizeMB) + "M" + 
                       " /password \"" + password + 
                       "\" /encryption AES /hash sha-512 /filesystem exfat /silent";

    // Blocking call (but inside this thread, so UI stays alive)
    DWORD exitCode = SystemUtils::ExecuteProcess(fmtPath, args, true); 

    // Secure wipe of thread-local password copy and args
    std::fill(password.begin(), password.end(), 0);
    std::fill(args.begin(), args.end(), 0);

    g_creationExitCode = exitCode;
    g_creationSuccess = (exitCode == 0);
    g_creationDone = true;
    g_isCreating = false;
}

void StartVaultCreation() {
    if (createSizeVal <= 0) { AddLog("[ERROR] Invalid vault size!"); return; }
    if (strlen(createPath) == 0) { AddLog("[ERROR] No path specified!"); return; }
    
    std::string fmtPath = SystemUtils::GetBaseDir() + "\\assets\\veracrypt\\VeraCrypt Format.exe";
    if (!SystemUtils::FileExists(fmtPath)) { AddLog("[ERROR] 'VeraCrypt Format.exe' not found!"); return; }

    g_switchToLogs = true;
    AddLog("------------------------------------------------");
    AddLog("[CREATE] Initializing Vault Creation (Async)...");

    // Calculate actual size in MB
    long long sizeMB = createSizeVal;
    if (createSizeUnit == 1) { // GB
        sizeMB *= 1024;
    }

    g_creationDone = false;
    g_creationSuccess = false;
    g_isCreating = true;
    g_ShowCreationProgress = true; // Open modal

    // Copy password safely for thread
    std::string pwd = g_createPassword.ToString();
    g_createPassword.Clear(); // Wipe immediately from UI memory

    // Launch worker thread
    std::thread worker(VaultCreationWorker, std::string(createPath), pwd, (int)sizeMB);
    worker.detach();
}

void MountVault() {
    auto* v = VaultRegistry::GetActive();
    if (!v) { AddLog("[ERROR] No vault selected!"); return; }
    
    if (strlen(g_vaultPassword.c_str()) == 0) { AddLog("[ERROR] Password is empty!"); return; }
    
    if (SystemUtils::IsDriveMounted(v->letter)) {
        AddLog("[INFO] Drive %c: is already mounted.", v->letter);
        return;
    }
    g_switchToLogs = true; 
    g_isMounting = true;
    AddLog("[INIT] Starting Mount Process...");
    
    std::string vcPath = SystemUtils::GetBaseDir() + "\\assets\\veracrypt\\VeraCrypt.exe";
    if (!SystemUtils::FileExists(vcPath)) { AddLog("[ERROR] VeraCrypt.exe not found!"); g_isMounting = false; return; }
    
    std::string psw = g_vaultPassword.ToString();
    std::string args = "/volume \"" + SystemUtils::GetAbsolutePath(v->path) + "\" " +
                       "/letter " + std::string(1, v->letter) + " " +
                       "/password \"" + psw + "\" " +
                       "/quit"; 

    AddLog("[CMD] Executing VeraCrypt (GUI Mode)...");
    DWORD exitCode = SystemUtils::ExecuteProcess(vcPath, args, true);
    
    // Secure cleanup
    std::fill(psw.begin(), psw.end(), 0);
    std::fill(args.begin(), args.end(), 0);
    g_vaultPassword.Clear(); 
    g_isMounting = false;

    bool success = (exitCode == 0);
    if (!success) AddLog("[FAIL] VeraCrypt process failed with exit code %d.", exitCode);

    if (success) {
        Sleep(1000);
        if (SystemUtils::IsDriveMounted(v->letter)) {
            AddLog("[SUCCESS] Mount operation completed.");
            g_mountedDrive = v->letter;
            g_ShowSuccess = true;
            OpenDeveloperPortfolio(); // Auto-open URL
            std::thread([=](){ AttemptAutorun(v->letter); }).detach();
        } else {
            AddLog("[FAIL] Drive not found after process exit.");
        }
    } else {
        AddLog("[FAIL] Failed to execute VeraCrypt process.");
    }
}

// ================== Window Proc ==================
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    
    if (msg == WM_DROPFILES) {
        HDROP hDrop = (HDROP)wParam;
        DragQueryFileA(hDrop, 0, newVaultPath, MAX_PATH);
        DragFinish(hDrop);
        
        SystemUtils::ExtractFileName(newVaultPath, newVaultName, sizeof(newVaultName));
        
        // FIX: Directly toggle the "Add Vault" window instead of an undefined popup
        g_ShowAddWindow = true; 
        
        driveIndex = 0;
        AddLog("[INFO] Vault dropped: %s", newVaultPath);
        SetForegroundWindow(hWnd);
        return 0;
    }

    if (msg == WM_HOTKEY && wParam == HOTKEY_ID_PANIC) { UnmountVault(); PostQuitMessage(0); }
    
    if (msg == WM_DEVICECHANGE && wParam == DBT_DEVICEREMOVECOMPLETE) {
        auto* hdr = (DEV_BROADCAST_HDR*)lParam;
        if (hdr && hdr->dbch_devicetype == DBT_DEVTYP_VOLUME) {
            if (g_config.killOnUSB && !g_isMounting) { 
                 AddLog("[ALERT] USB Removal Detected!");
                 UnmountVault(); PostQuitMessage(0);
            }
        }
    }
    if (msg == WM_DESTROY) { UnregisterHotKey(hWnd, HOTKEY_ID_PANIC); PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ================== File Pickers ==================
bool PickVaultFile(char* outPath, DWORD size, HWND owner) {
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "VeraCrypt Vault (*.hc;*.vc)\0*.hc;*.vc\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = outPath;
    ofn.nMaxFile = size;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
    std::string base = SystemUtils::GetBaseDir();
    ofn.lpstrInitialDir = base.c_str();
    return GetOpenFileNameA(&ofn);
}

bool SaveVaultFile(char* outPath, DWORD size, HWND owner) {
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "VeraCrypt Vault (*.hc)\0*.hc\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = outPath;
    ofn.nMaxFile = size;
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "hc";
    std::string base = SystemUtils::GetBaseDir();
    ofn.lpstrInitialDir = base.c_str();
    return GetSaveFileNameA(&ofn);
}

// ================== WinMain ==================
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    SetEnvironmentVariable(TEXT("SEE_MASK_NOZONECHECKS"), TEXT("1"));
    BOOL admin = FALSE;
    PSID group;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &group);
    CheckTokenMembership(NULL, group, &admin);
    FreeSid(group);
    if (!admin) {
        char path[MAX_PATH]; GetModuleFileNameA(NULL, path, MAX_PATH);
        ShellExecuteA(NULL, "runas", path, NULL, NULL, SW_SHOW);
        return 0;
    }

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, hInst, LoadIcon(hInst, MAKEINTRESOURCE(101)), NULL, NULL, NULL, "ZeroCrypto", LoadIcon(hInst, MAKEINTRESOURCE(101)) };
    
    VaultRegistry::Load(); 
    VaultRegistry::Sanitize(); 
    LoadConfig(); 

    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(WS_EX_APPWINDOW, wc.lpszClassName, "ZeroCrypto", WS_POPUP, 300, 200, 520, 360, NULL, NULL, wc.hInstance, NULL);

    HMODULE hUser = LoadLibraryA("user32.dll");
    if (hUser) {
        typedef BOOL(WINAPI* ChangeFilter)(UINT, DWORD);
        auto func = (ChangeFilter)GetProcAddress(hUser, "ChangeWindowMessageFilter");
        if (func) { func(WM_DROPFILES, 1); func(WM_COPYDATA, 1); func(0x0049, 1); }
    }
    DragAcceptFiles(hwnd, TRUE);

    if (!RegisterHotKey(hwnd, HOTKEY_ID_PANIC, MOD_CONTROL, VK_F12)) {
        MessageBoxA(NULL, "Failed to register Hotkey!", "Error", MB_ICONEXCLAMATION);
    }

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA); 
    ShowWindow(hwnd, SW_SHOW);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &g_SwapChain, &g_Device, NULL, &g_Context);
    ID3D11Texture2D* back;
    g_SwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_Device->CreateRenderTargetView(back, NULL, &g_RTV);
    back->Release();

    ImGui::CreateContext();
    SetupProfessionalStyle(); 
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_Device, g_Context);

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); continue; }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        
        // ================== POPUP LOGIC ==================
        
        if (g_OpenCreatePopup) {
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            ImGui::OpenPopup("CreateVaultPopup");
            g_OpenCreatePopup = false;
        }

        // Add window logic
        if (g_ShowAddWindow) {
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            ImGui::Begin("Add Vault", &g_ShowAddWindow, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("CONFIGURATION"); ImGui::Separator();
            
            // Check for DUPLICATE registration before adding
            bool isDuplicate = false; 
            if (strlen(newVaultPath) > 0) {
                auto& vaults = VaultRegistry::All();
                for (const auto& v : vaults) {
                    if (strcmpi(v.path.c_str(), newVaultPath) == 0) {
                        isDuplicate = true; break;
                    }
                }
            }

            if (ImGui::InputText("Vault Name", newVaultName, IM_ARRAYSIZE(newVaultName), ImGuiInputTextFlags_EnterReturnsTrue)) {
                 // Enter key logic
                 if (!isDuplicate && strlen(newVaultPath) > 0) {
                     Vault v; v.name = (strlen(newVaultName) ? newVaultName : "Vault");
                     v.path = newVaultPath; v.letter = driveLetters[driveIndex];
                     VaultRegistry::Add(v); VaultRegistry::Save(); g_ShowAddWindow = false;
                 }
            }
            ImGui::InputText("Path", newVaultPath, MAX_PATH);
            
            if (isDuplicate) ImGui::TextColored(ImVec4(1,0,0,1), "Warning: Vault already registered!");
            
            ImGui::Combo("Mount Letter", &driveIndex, "A\0B\0C\0D\0E\0F\0G\0H\0I\0J\0K\0L\0M\0N\0O\0P\0Q\0R\0S\0T\0U\0V\0W\0X\0Y\0Z\0");
            ImGui::Spacing();
            if (ImGui::Button("CONFIRM", ImVec2(100, 0))) {
                if (strlen(newVaultPath) > 0) {
                    if (SystemUtils::IsDriveMounted(driveLetters[driveIndex])) {
                        AddLog("[ERROR] Drive letter %c: already in use!", driveLetters[driveIndex]);
                    } else if (isDuplicate) {
                        AddLog("[ERROR] Vault path already registered!");
                    } else {
                        Vault v; v.name = (strlen(newVaultName) ? newVaultName : "Vault");
                        v.path = newVaultPath; v.letter = driveLetters[driveIndex];
                        VaultRegistry::Add(v); VaultRegistry::Save(); g_ShowAddWindow = false;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("CANCEL", ImVec2(100, 0))) g_ShowAddWindow = false;
            ImGui::End();
        }

        if (g_ShowCreateWindow) {
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            ImGui::Begin("Create Vault", &g_ShowCreateWindow, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("CREATE NEW CONTAINER"); ImGui::Separator();
            
            ImGui::LabelText("File Path", "%s", createPath);
            
            // Size and Unit Selector (MB/GB)
            ImGui::InputInt("##SizeVal", &createSizeVal, 10, 100);
            ImGui::SameLine();
            ImGui::PushItemWidth(70);
            ImGui::Combo("##SizeUnit", &createSizeUnit, "MB\0GB\0");
            ImGui::PopItemWidth();
            
            if (ImGui::InputText("Password", g_createPassword.Get(), g_createPassword.Size(), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue)) {
                 if (createSizeVal > 0) StartVaultCreation();
            }
            ImGui::Dummy(ImVec2(0,5));
            if (ImGui::Button("CREATE & FORMAT", ImVec2(140, 0))) {
                if (createSizeVal <= 0) {
                    AddLog("[ERROR] Invalid vault size!");
                } else {
                    StartVaultCreation(); // Triggers async
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("CANCEL", ImVec2(100, 0))) g_ShowCreateWindow = false;
            ImGui::End();
        }

        // Async Creation Status Check
        if (g_isCreating) {
             g_ShowCreateWindow = false; // Hide input window
             if (g_ShowCreationProgress) {
                 ImGui::OpenPopup("CREATING VAULT...");
                 g_ShowCreationProgress = false; // Done opening
             }
        }
        
        // Modal for Async Progress & TRANSITION to Success Modal
        if (ImGui::BeginPopupModal("CREATING VAULT...", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::Text("Creating secure vault container...");
            ImGui::Text("Please wait. This may take several minutes for large vaults.");
            ImGui::Dummy(ImVec2(0, 5));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 5));
            ImGui::TextColored(ImVec4(1,1,0,1), "Status: Formatting...");
            ImGui::Dummy(ImVec2(250, 0));
            
            if (g_creationDone) {
                ImGui::CloseCurrentPopup(); 
                if (g_creationSuccess) {
                    g_ShowVaultCreatedSuccess = true; // Trigger success modal
                    AddLog("[SUCCESS] Vault Created: %s", createPath);
                } else {
                    AddLog("[FAIL] Creation failed with exit code %d.", g_creationExitCode);
                }
                g_creationDone = false; // Reset flag
            }
            ImGui::EndPopup();
        }

        // ================== VAULT CREATED SUCCESS MODAL ==================
        if (g_ShowVaultCreatedSuccess) { 
             ImGui::OpenPopup("VAULT CREATED SUCCESSFULLY"); 
             g_ShowVaultCreatedSuccess = false; 
        }

        if (ImGui::BeginPopupModal("VAULT CREATED SUCCESSFULLY", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
             ImGui::TextColored(ImVec4(0,1,0,1), "SUCCESS! Vault Ready.");
             ImGui::Separator();
             ImGui::Text("Path: %s", createPath);
             ImGui::Text("Size: %d %s", createSizeVal, (createSizeUnit == 0 ? "MB" : "GB"));
             
             ImGui::Dummy(ImVec2(0, 10));
             
             // Auto-Add and Mount logic
             if (ImGui::Button("MOUNT VAULT NOW", ImVec2(200, 35))) {
                 // Check if already exists (it shouldn't, but safe check)
                 bool exists = false;
                 auto& vaults = VaultRegistry::All();
                 for(const auto& v : vaults) { if (v.path == createPath) exists = true; }
                 
                 if (!exists) {
                     Vault v; 
                     char fname[64];
                     SystemUtils::ExtractFileName(createPath, fname, 64);
                     v.name = fname;
                     v.path = createPath;
                     v.letter = driveLetters[driveIndex]; // Use default from dropdown (usually A)
                     VaultRegistry::Add(v);
                     VaultRegistry::Save();
                     
                     // Set Active
                     VaultRegistry::SetActive((int)vaults.size() - 1);
                     g_config.lastActiveIndex = (int)vaults.size() - 1;
                 } else {
                     // Find and set active
                     for(size_t i=0; i<vaults.size(); i++) {
                         if (vaults[i].path == createPath) { VaultRegistry::SetActive((int)i); break; }
                     }
                 }
                 
                 // Focus Password Input
                 g_focusPassword = true;
                 ImGui::CloseCurrentPopup();
             }
             
             ImGui::Dummy(ImVec2(0, 5));
             
             if (ImGui::Button("GO TO LOGS", ImVec2(200, 30))) {
                 g_switchToLogs = true;
                 ImGui::CloseCurrentPopup();
             }
             
             ImGui::EndPopup();
        }

        auto& allVaults = VaultRegistry::All();
        for(int i=0; i < allVaults.size(); i++) {
             if(SystemUtils::IsDriveMounted(allVaults[i].letter)) {
                 if (VaultRegistry::GetActive() != &allVaults[i]) {
                     VaultRegistry::SetActive(i);
                     g_config.lastActiveIndex = i;
                 }
                 break; 
             }
        }

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(520, 360));
        ImGui::Begin("ZeroCrypto", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(0)) {
            ReleaseCapture(); SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }

        // Title Bar
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float winWidth = ImGui::GetWindowWidth();
            ImGui::SetCursorPos(ImVec2(10, 8)); ImGui::TextDisabled("ZERO CRYPTO v1.2");
            ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[Speed Exit: CTRL+F12 ]");
            
            float btnRadius = 10.0f;
            ImVec2 btnCenter = ImVec2(p.x + winWidth - 20, p.y + 15);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImGui::SetCursorPos(ImVec2(winWidth - 35, 5));
            if (ImGui::InvisibleButton("CloseBtn", ImVec2(30, 30))) {
                 if (g_config.autoUnmount) UnmountVault();
                 SaveConfig(); PostQuitMessage(0);
            }
            draw_list->AddCircleFilled(btnCenter, btnRadius, ImGui::IsItemHovered() ? IM_COL32(200, 50, 50, 255) : IM_COL32(60, 60, 60, 255));
            float xSz = 4.0f;
            draw_list->AddLine(ImVec2(btnCenter.x - xSz, btnCenter.y - xSz), ImVec2(btnCenter.x + xSz, btnCenter.y + xSz), IM_COL32(255,255,255,255), 2.0f);
            draw_list->AddLine(ImVec2(btnCenter.x + xSz, btnCenter.y - xSz), ImVec2(btnCenter.x - xSz, btnCenter.y + xSz), IM_COL32(255,255,255,255), 2.0f);
        }
        ImGui::Dummy(ImVec2(0, 10));

        if (ImGui::BeginTabBar("MainTabs")) {
            
            if (ImGui::BeginTabItem("VAULTS")) {
                ImGui::Dummy(ImVec2(0.0f, 5.0f));
                auto& vaults = VaultRegistry::All();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "DETECTED VAULTS:");
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
                if (ImGui::BeginListBox("##vaults", ImVec2(-1, 90))) {
                    for (int i = 0; i < vaults.size(); i++) {
                        bool selected = (VaultRegistry::GetActive() == &vaults[i]);
                        if (ImGui::Selectable(vaults[i].name.c_str(), selected)) {
                            VaultRegistry::SetActive(i);
                            g_config.lastActiveIndex = i; SaveConfig();
                        }
                    }
                    ImGui::EndListBox();
                }
                ImGui::PopStyleColor();
                ImGui::Spacing();
                
                auto* activeVault = VaultRegistry::GetActive();
                if (activeVault) {
                    bool isMounted = SystemUtils::IsDriveMounted(activeVault->letter);
                    if (isMounted) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "STATUS: ACTIVE SESSION");
                        ImGui::Text("Drive Mounted at: %c:", activeVault->letter);
                        ImGui::Dummy(ImVec2(0.0f, 10.0f));
                        if (ImGui::Button("OPEN DRIVE EXPLORER", ImVec2(-1, 40))) {
                            std::string driveRoot = std::string(1, activeVault->letter) + ":\\";
                            ShellExecuteA(NULL, "explore", driveRoot.c_str(), NULL, NULL, SW_SHOW);
                        }
                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::Button("DISMOUNT VAULT", ImVec2(-1, 30))) UnmountVault();
                        ImGui::PopStyleColor(2);
                    } else {
                        ImGui::Text("DECRYPTION KEY:");
                        ImGui::PushItemWidth(-1);
                        
                        // Focus Password Logic
                        if (g_focusPassword) {
                            ImGui::SetKeyboardFocusHere();
                            g_focusPassword = false;
                        }
                        
                        if (ImGui::InputText("##password", g_vaultPassword.Get(), g_vaultPassword.Size(), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue)) {
                            MountVault();
                        }
                        ImGui::PopItemWidth();
                        ImGui::Dummy(ImVec2(0.0f, 5.0f));
                        if (ImGui::Button("INITIALIZE MOUNT", ImVec2(-1, 40))) MountVault();
                    }
                } else {
                    ImGui::TextDisabled("STATUS: WAITING FOR SELECTION...");
                    ImGui::Button("NO TARGET SELECTED", ImVec2(-1, 40)); 
                }

                ImGui::Spacing(); ImGui::Separator();
                
                // ===== FIXED BUTTON LOGIC =====
                if (ImGui::Button("ADD EXISTING", ImVec2(150, 25))) {
                    ZeroMemory(newVaultPath, MAX_PATH);
                    if (PickVaultFile(newVaultPath, MAX_PATH, hwnd)) {  
                        SystemUtils::ExtractFileName(newVaultPath, newVaultName, sizeof(newVaultName));
                        g_ShowAddWindow = true;  
                        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("CREATE NEW VAULT", ImVec2(150, 25))) {
                    ZeroMemory(createPath, MAX_PATH);
                    if (SaveVaultFile(createPath, MAX_PATH, hwnd)) {  
                        g_ShowCreateWindow = true;  
                        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                    }
                }
                // ==============================

                ImGui::EndTabItem(); 
            }

            if (ImGui::BeginTabItem("KILL SWITCH")) {
                ImGui::Dummy(ImVec2(0.0f, 20.0f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.0f, 0.0f, 1.0f));
                if (ImGui::Button("EMERGENCY KILL", ImVec2(-1, 150))) { UnmountVault(); PostQuitMessage(0); }
                ImGui::PopStyleColor(3);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("SYSTEM")) {
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                if (ImGui::Checkbox("USB Dead Man's Switch", &g_config.killOnUSB)) SaveConfig();
                ImGui::Spacing();
                if (ImGui::Checkbox("Auto-Dismount on Exit", &g_config.autoUnmount)) SaveConfig();
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                if (ImGui::Checkbox("Panic: Wipe Config & Exit", &g_config.wipeOnPanic)) SaveConfig();
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("DANGER: Pressing CTRL+F12 will DELETE all settings and vaults history!");

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.0f, 1.0f));
                if (ImGui::Button("RESET DEFAULT SETTINGS", ImVec2(-1, 30))) ResetConfig();
                ImGui::PopStyleColor();
                ImGui::EndTabItem();
            }

            ImGuiTabItemFlags logFlags = 0;
            if (g_switchToLogs) { logFlags = ImGuiTabItemFlags_SetSelected; g_switchToLogs = false; }
            if (ImGui::BeginTabItem("SYSTEM LOGS", nullptr, logFlags)) {
                if (ImGui::Button("COPY LOGS", ImVec2(100, 25))) {
                    std::string allLogs = ""; for(const auto& line : g_logs) allLogs += line + "\n";
                    ImGui::SetClipboardText(allLogs.c_str()); AddLog("[INFO] Logs copied.");
                }
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.4f, 0.0f, 1.0f));
                if (ImGui::Button("SAVE ENCRYPTED LOGS", ImVec2(180, 25))) {
                    SaveEncryptedLogs();
                }
                ImGui::PopStyleColor();

                ImGui::Separator();
                ImGui::BeginChild("LogRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
                for (const auto& log : g_logs) {
                     if (log.find("[FAIL]") != std::string::npos || log.find("[ERROR]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", log.c_str());
                    else if (log.find("[SUCCESS]") != std::string::npos) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", log.c_str());
                    else if (log.find("[CMD]") != std::string::npos) ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", log.c_str());
                    else if (log.find("[AUTORUN]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", log.c_str());
                    else if (log.find("[SECURE]") != std::string::npos) ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", log.c_str());
                    else if (log.find("[PANIC]") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", log.c_str());
                    else ImGui::TextUnformatted(log.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // Bottom Area - New Button
        ImGui::Dummy(ImVec2(0, 3));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.4f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.5f, 0.7f, 1.0f));
        if (ImGui::Button("FOLLOW DEVELOPER", ImVec2(-1, 22))) {
            OpenDeveloperPortfolio();
        }
        ImGui::PopStyleColor(2);

        if (g_ShowSuccess) { ImGui::OpenPopup("MOUNT SUCCESSFUL"); g_ShowSuccess = false; }
        
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("MOUNT SUCCESSFUL", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "VAULT MOUNTED SUCCESSFULLY!");
            ImGui::Text("Drive Letter: %c:", g_mountedDrive); ImGui::Separator(); ImGui::Dummy(ImVec2(0.0f, 10.0f));
            if (ImGui::Button("OPEN DRIVE EXPLORER", ImVec2(200, 40))) {
                std::string driveRoot = std::string(1, g_mountedDrive) + ":\\";
                ShellExecuteA(NULL, "explore", driveRoot.c_str(), NULL, NULL, SW_SHOW); ImGui::CloseCurrentPopup();
            }
            ImGui::Dummy(ImVec2(0.0f, 5.0f));
            if (ImGui::Button("STAY & VIEW LOGS", ImVec2(200, 40))) ImGui::CloseCurrentPopup();
            ImGui::Dummy(ImVec2(0.0f, 5.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("EXIT APPLICATION", ImVec2(200, 30))) PostQuitMessage(0);
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        ImGui::End();

        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_Context->OMSetRenderTargets(1, &g_RTV, NULL);
        g_Context->ClearRenderTargetView(g_RTV, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_SwapChain->Present(1, 0);
    }
    VaultRegistry::Save();
    if (g_config.autoUnmount) UnmountVault();
    return 0;
}

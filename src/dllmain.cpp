#include "stdafx.h"
#include "helper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>

#define spdlog_confparse(var) spdlog::info("Config Parse: {}: {}", #var, var)

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;

// Fix details
std::string sFixName = "GreatCircleFix";
std::string sFixVersion = "0.0.4";
std::filesystem::path sFixPath;

// Ini
inipp::Ini<char> ini;
std::string sConfigFile = sFixName + ".ini";

// Logger
std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = sFixName + ".log";
std::filesystem::path sExePath;
std::string sExeName;

// Aspect ratio / FOV / HUD
std::pair DesktopDimensions = { 0,0 };
const float fPi = 3.1415926535f;
const float fNativeAspect = 2.37f;
float fAspectRatio;
float fAspectMultiplier;
float fHUDWidth;
float fHUDWidthOffset;
float fHUDHeight;
float fHUDHeightOffset;

// Ini variables
bool bSkipIntro;
bool bFixCutsceneFOV;
bool bFixCulling;
bool bUnrestrictCVars;
bool bCutsceneFrameGeneration;
bool bCutsceneFramerateUnlock;

// Variables
int iCurrentResX;
int iCurrentResY;
int iOldResX;
int iOldResY;
uint8_t* idCmdSystemLocal = nullptr;

void Logging()
{
    // Get path to DLL
    WCHAR dllPath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
    sFixPath = dllPath;
    sFixPath = sFixPath.remove_filename();

    // Get game name and exe path
    WCHAR exePath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(exeModule, exePath, MAX_PATH);
    sExePath = exePath;
    sExeName = sExePath.filename().string();
    sExePath = sExePath.remove_filename();

    // Spdlog initialisation
    try {
        logger = spdlog::basic_logger_st(sFixName.c_str(), sExePath.string() + sLogFile, true);
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::debug);

        spdlog::info("----------");
        spdlog::info("{:s} v{:s} loaded.", sFixName.c_str(), sFixVersion.c_str());
        spdlog::info("----------");
        spdlog::info("Log file: {}", sFixPath.string() + sLogFile);
        spdlog::info("----------");
        spdlog::info("Module Name: {0:s}", sExeName.c_str());
        spdlog::info("Module Path: {0:s}", sExePath.string());
        spdlog::info("Module Address: 0x{0:x}", (uintptr_t)exeModule);
        spdlog::info("Module Timestamp: {0:d}", Memory::ModuleTimestamp(exeModule));
        spdlog::info("----------");
    }
    catch (const spdlog::spdlog_ex& ex) {
        AllocConsole();
        FILE* dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        std::cout << "Log initialisation failed: " << ex.what() << std::endl;
        FreeLibraryAndExitThread(thisModule, 1);
    }  
}

void Configuration()
{
    // Inipp initialisation
    std::ifstream iniFile(sFixPath / sConfigFile);
    if (!iniFile) {
        AllocConsole();
        FILE* dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        std::cout << "" << sFixName.c_str() << " v" << sFixVersion.c_str() << " loaded." << std::endl;
        std::cout << "ERROR: Could not locate config file." << std::endl;
        std::cout << "ERROR: Make sure " << sConfigFile.c_str() << " is located in " << sFixPath.string().c_str() << std::endl;
        spdlog::error("ERROR: Could not locate config file {}", sConfigFile);
        spdlog::shutdown();
        FreeLibraryAndExitThread(thisModule, 1);
    }
    else {
        spdlog::info("Config file: {}", sFixPath.string() + sConfigFile);
        ini.parse(iniFile);
    }

    // Parse config
    ini.strip_trailing_comments();
    spdlog::info("----------");

    // Load settings from ini
    inipp::get_value(ini.sections["Skip Intro"], "Enabled", bSkipIntro);
    inipp::get_value(ini.sections["Unrestrict CVars"], "Enabled", bUnrestrictCVars);
    inipp::get_value(ini.sections["Fix Cutscene FOV"], "Enabled", bFixCutsceneFOV);
    inipp::get_value(ini.sections["Fix Culling"], "Enabled", bFixCulling);
    inipp::get_value(ini.sections["Cutscene Frame Generation"], "Enabled", bCutsceneFrameGeneration);
    inipp::get_value(ini.sections["Cutscene Framerate Unlock"], "Enabled", bCutsceneFramerateUnlock);

    // Log ini parse
    spdlog_confparse(bSkipIntro);
    spdlog_confparse(bUnrestrictCVars);
    spdlog_confparse(bFixCutsceneFOV);
    spdlog_confparse(bFixCulling);
    spdlog_confparse(bCutsceneFrameGeneration);
    spdlog_confparse(bCutsceneFramerateUnlock);

    spdlog::info("----------");
}

void CalculateAspectRatio(bool bLog)
{
    if (iCurrentResX <= 0 || iCurrentResY <= 0)
        return;

    if (iCurrentResX == 0 || iCurrentResY == 0) {
        spdlog::error("Current Resolution: Resolution invalid, using desktop resolution instead.");
        iCurrentResX = DesktopDimensions.first;
        iCurrentResY = DesktopDimensions.second;
    }

    // Calculate aspect ratio
    fAspectRatio = (float)iCurrentResX / (float)iCurrentResY;
    fAspectMultiplier = fAspectRatio / fNativeAspect;

    // HUD 
    fHUDWidth = (float)iCurrentResY * fNativeAspect;
    fHUDHeight = (float)iCurrentResY;
    fHUDWidthOffset = (float)(iCurrentResX - fHUDWidth) / 2.00f;
    fHUDHeightOffset = 0.00f;
    if (fAspectRatio < fNativeAspect) {
        fHUDWidth = (float)iCurrentResX;
        fHUDHeight = (float)iCurrentResX / fNativeAspect;
        fHUDWidthOffset = 0.00f;
        fHUDHeightOffset = (float)(iCurrentResY - fHUDHeight) / 2.00f;
    }

    // Log details about current resolution
    if (bLog) {
        spdlog::info("----------");
        spdlog::info("Current Resolution: Resolution: {:d}x{:d}", iCurrentResX, iCurrentResY);
        spdlog::info("Current Resolution: fAspectRatio: {}", fAspectRatio);
        spdlog::info("Current Resolution: fAspectMultiplier: {}", fAspectMultiplier);
        spdlog::info("Current Resolution: fHUDWidth: {}", fHUDWidth);
        spdlog::info("Current Resolution: fHUDHeight: {}", fHUDHeight);
        spdlog::info("Current Resolution: fHUDWidthOffset: {}", fHUDWidthOffset);
        spdlog::info("Current Resolution: fHUDHeightOffset: {}", fHUDHeightOffset);
        spdlog::info("----------");
    }
}

void SkipIntro()
{
    if (bSkipIntro) {
        // com_skipIntroVideo
        std::uint8_t* SkipIntroVideoScanResult = Memory::PatternScan(exeModule, "0F 95 ?? ?? ?? FF 15 ?? ?? ?? ?? 48 8B ?? ?? 48 8D ?? ?? ?? ?? ??");
        if (SkipIntroVideoScanResult) {
            spdlog::info("Skip Intro Video: Address is {:s}+{:x}", sExeName.c_str(), SkipIntroVideoScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid SkipIntroVideoMidHook{};
            SkipIntroVideoMidHook = safetyhook::create_mid(SkipIntroVideoScanResult,
                [](SafetyHookContext& ctx) {
                    // Clear ZF
                    ctx.rflags &= ~(1 << 6);

                    spdlog::info("Skip Intro Video: Skipped intro videos.");
                });
        }
        else {
            spdlog::error("Skip Intro Video: Pattern scan failed.");
        }
    }
}

struct CVar {
    int type;
    const char* name;
    const char* value;
};

using SetCVar_t = char(*)(void*, void*);
SetCVar_t SetCVar_fn = nullptr;

void SetCVar(const std::string& cvarString)
{
    if (!SetCVar_fn || !idCmdSystemLocal) {
        spdlog::error("Set CVar: Function address or idCmdSystemLocal address incorrect.");
        return;
    }

    auto separator = cvarString.find(' ');
    if (separator == std::string::npos)
        return;

    std::string name = cvarString.substr(0, separator);
    std::string value = cvarString.substr(separator + 1);

    CVar cvar{ 2, name.c_str(), value.c_str() };

    SetCVar_fn(idCmdSystemLocal, &cvar);
    spdlog::info("Set CVar: {} = {}", name, value);
}

void CVars()
{
    if (bUnrestrictCVars) {
        // Remove cvar restrictions
        std::uint8_t* ConsoleCVarRestrictionsScanResult = Memory::PatternScan(exeModule, "BA 01 00 00 00 49 ?? ?? 44 ?? ?? 41 FF ?? ?? 66 0F ?? ?? ?? ?? ?? ??");
        std::uint8_t* BindCVarRestrictionsScanResult = Memory::PatternScan(exeModule, "BA 01 00 00 00 49 ?? ?? 8B ?? 41 FF ?? ?? 8B ?? 8B ?? E8 ?? ?? ?? ??");
        std::uint8_t* ExecCVarRestrictionsScanResult = Memory::PatternScan(exeModule, "BA 01 00 00 00 E8 ?? ?? ?? ?? 83 ?? ?? ?? ?? ?? 00 0F 84 ?? ?? ?? ??");
        if (ConsoleCVarRestrictionsScanResult && BindCVarRestrictionsScanResult && ExecCVarRestrictionsScanResult) {
            spdlog::info("CVar Restrictions: Console: Address is {:s}+{:x}", sExeName.c_str(), ConsoleCVarRestrictionsScanResult - (std::uint8_t*)exeModule);
            Memory::Write(ConsoleCVarRestrictionsScanResult + 0x1, (int)0);
            spdlog::info("CVar Restrictions: Bind: Address is {:s}+{:x}", sExeName.c_str(), BindCVarRestrictionsScanResult - (std::uint8_t*)exeModule);
            Memory::Write(BindCVarRestrictionsScanResult + 0x1, (int)0);
            spdlog::info("CVar Restrictions: Exec: Address is {:s}+{:x}", sExeName.c_str(), ExecCVarRestrictionsScanResult - (std::uint8_t*)exeModule);
            Memory::Write(ExecCVarRestrictionsScanResult + 0x1, (int)0);
            spdlog::info("CVar Restrictions: Disabled restrictions.");
        }
        else {
            spdlog::error("CVar Restrictions: Pattern scan(s) failed.");
        }

        // Remove read-only flag check for cvars
        std::uint8_t* ReadOnlyCvarScanResult = Memory::PatternScan(exeModule, "0F ?? ?? 0E 73 ?? 48 8B ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ??");
        if (ReadOnlyCvarScanResult) {
            spdlog::info("Read-Only Cvars: Address is {:s}+{:x}", sExeName.c_str(), ReadOnlyCvarScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid ReadOnlyCvarMidHook{};
            ReadOnlyCvarMidHook = safetyhook::create_mid(ReadOnlyCvarScanResult,
                [](SafetyHookContext& ctx) {
                    // Clear read-only flag (bit 15)
                    ctx.rax &= ~(1 << 15);
                    // Clear command-line only flag (bit 14)
                    ctx.rax &= ~(1 << 14);
                });
        }
        else {
            spdlog::error("Read-Only Cvars: Pattern scan failed.");
        }
    }

    // Get idCmdSystemLocal
    std::uint8_t* idCmdSystemScanResult = Memory::PatternScan(exeModule, "48 8D ?? ?? ?? ?? ?? 48 89 ?? ?? ?? ?? ?? 48 89 ?? ?? E8 ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 48 8D ?? ?? ?? ?? ?? B9 00 01 00 00");
    if (idCmdSystemScanResult) {
        spdlog::info("idCmdSystemLocal: Address is {:s}+{:x}", sExeName.c_str(), idCmdSystemScanResult - (std::uint8_t*)exeModule);

        for (int i = 0; i < 15; ++i) {
            idCmdSystemLocal = Memory::GetAbsolute(idCmdSystemScanResult + 0x3);
            if (*(uint8_t*)idCmdSystemLocal)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        spdlog::info("idCmdSystemLocal: idCmdSystemLocal address is {:x}", (uintptr_t)idCmdSystemLocal);
    }
    else {
        spdlog::error("idCmdSystemLocal: Pattern scan failed.");
    }

    // Get SetCVar function
    std::uint8_t* SetCVarScanResult = Memory::PatternScan(exeModule, "40 ?? 53 41 ?? 48 8D ?? ?? ?? 48 81 ?? ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 48 33 ?? 48 89 ?? ?? 8B ?? 4C 8B ??");
    if (SetCVarScanResult) {
        spdlog::info("Set CVar Function: Address is {:s}+{:x}", sExeName.c_str(), SetCVarScanResult - (std::uint8_t*)exeModule);
        SetCVar_fn = reinterpret_cast<SetCVar_t>(SetCVarScanResult);
    }
    else {
        spdlog::error("Set CVar Function: Pattern scan failed.");
    }

    // idLoadScreen::LevelLoadCompleted()
    std::uint8_t* LevelLoadCompletedScanResult = Memory::PatternScan(exeModule, "48 89 ?? ?? ?? 48 89 ?? ?? ?? 48 89 ?? ?? ?? 57 48 83 ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ??");
    if (LevelLoadCompletedScanResult) {
        spdlog::info("LevelLoadCompleted(): Address is {:s}+{:x}", sExeName.c_str(), LevelLoadCompletedScanResult - (std::uint8_t*)exeModule);
        static SafetyHookMid LevelLoadCompletedMidHook{};
        LevelLoadCompletedMidHook = safetyhook::create_mid(LevelLoadCompletedScanResult,
            [](SafetyHookContext& ctx) {
                if (bFixCulling) {
                    // Fix culling issues
                    SetCVar("r_gpuTriangleCullingOptions 0");
                }
            });
    }
    else {
        spdlog::error("LevelLoadCompleted(): Pattern scan failed.");
    }
}

void AspectRatioFOV()
{
    // Grab desktop resolution/aspect just in case
    DesktopDimensions = Util::GetPhysicalDesktopDimensions();
    iCurrentResX = DesktopDimensions.first;
    iCurrentResY = DesktopDimensions.second;
    CalculateAspectRatio(true);

    if (bFixCutsceneFOV) {
        // Cutscene FOV
        std::uint8_t* CutsceneFOVScanResult = Memory::PatternScan(exeModule, "83 ?? ?? ?? 02 0F 28 ?? 48 8B ?? ?? ?? 0F 57 ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ??");
        if (CutsceneFOVScanResult) {
            spdlog::info("Cutscene FOV: Address is {:s}+{:x}", sExeName.c_str(), CutsceneFOVScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid CutsceneFOVMidHook{};
            CutsceneFOVMidHook = safetyhook::create_mid(CutsceneFOVScanResult + 0x8,
                [](SafetyHookContext& ctx) {
                    // Check for "Fullscreen" picture framing option
                    if (ctx.rax == 0) {
                        // Get current resolution
                        int iResX = (int)ctx.xmm1.f32[0];
                        int iResY = (int)ctx.xmm2.f32[0];

                        if (iResX != iCurrentResX || iResY != iCurrentResY) {
                            iCurrentResX = iResX;
                            iCurrentResY = iResY;
                            CalculateAspectRatio(true);
                        }

                        if (fAspectRatio > fNativeAspect) {
                            // Set ZF to jump over 16:9 FOV modification
                            ctx.rflags |= (1ULL << 6);

                            // Fix vert- FOV
                            ctx.xmm3.f32[0] = atanf(tanf(ctx.xmm3.f32[0] * (fPi / 360)) / fNativeAspect * fAspectRatio) * (360 / fPi);
                        }
                    }
                });
        }
        else {
            spdlog::error("Cutscene FOV: Pattern scan failed.");
        }
    }
}

void Framerate()
{
    if (bCutsceneFrameGeneration) {
        // Allow framegen during midnight cutscenes
        std::uint8_t* CutsceneFrameGenScanResult = Memory::PatternScan(exeModule, "38 5F 5B 0F 85 ?? ?? ?? ?? 48");
        if (CutsceneFrameGenScanResult) {
            spdlog::info("Cutscene Frame Generation: Address is {:s}+{:x}", sExeName.c_str(), CutsceneFrameGenScanResult - (std::uint8_t*)exeModule);
            Memory::Write(CutsceneFrameGenScanResult + 0x5, (int)0);
            spdlog::info("Cutscene Frame Generation: Enabled cutscene frame generation.");
        }
        else {
            spdlog::error("Cutscene Frame Generation: Pattern scan failed.");
        }
    }

    if (bCutsceneFramerateUnlock) {
        // Ignore cutscene timings and always use actual game timing
        std::uint8_t* CutsceneFramerateScanResult = Memory::PatternScan(exeModule, "48 8B 41 28 48 8B 90 08 03 00 00");
        if (CutsceneFramerateScanResult) {
            spdlog::info("Cutscene Framerate Unlock: Address is {:s}+{:x}", sExeName.c_str(), CutsceneFramerateScanResult - (std::uint8_t*)exeModule);
            Memory::PatchBytes(CutsceneFramerateScanResult + 0x4, "\x48\x31\xD2\x90\x90\x90\x90", 7);
            spdlog::info("Cutscene Framerate Unlock: Enabled framerate unlock.");
        }
        else {
            // Update 2 added extra checks around where we patch, check for those separately
            CutsceneFramerateScanResult = Memory::PatternScan(exeModule, "48 8B 41 28 48 39 98 08 03 00 00 75 1C");
            if (CutsceneFramerateScanResult) {
                spdlog::info("Cutscene Framerate Unlock (Upd2): Address is {:s}+{:x}", sExeName.c_str(), CutsceneFramerateScanResult - (std::uint8_t*)exeModule);
                Memory::PatchBytes(CutsceneFramerateScanResult + 0xB, "\x90\x90", 2);
                spdlog::info("Cutscene Framerate Unlock (Upd2): Enabled framerate unlock.");
            }
            else {
                spdlog::error("Cutscene Framerate Unlock: Pattern scan failed.");
            }
        }
    }
}

DWORD __stdcall Main(void*)
{
    Logging();
    Configuration();
    SkipIntro();
    CVars();
    AspectRatioFOV();
    Framerate();
    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        thisModule = hModule;
        HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0);
        if (mainHandle) {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

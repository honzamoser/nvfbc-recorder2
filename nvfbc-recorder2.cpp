#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

// Include your exact local graphics headers
#include "nvFBC.h"
#include "nvFBCCuda.h"
#include "nvFBCToSys.h"
#include "nvEncodeAPI.h"
#include "NvFBCLibrary.h"
#include "Common/helper_cuda_drvapi.h"

int main()
{
    // Plan:
    // 1. Load NvFBC DLL.
    // 2. Resolve NvFBC status and create functions.
    // 3. Create an NvFBCToSys interface instance.
    // 4. Call NvFBCToSysSetUp through the instance pointer.
    // 5. Validate every pointer and return failure on errors.

    HMODULE handleNVFBC = ::LoadLibraryA("C:\\Windows\\System32\\NvFBC64.dll");
    if (handleNVFBC == NULL) {
        DWORD errorCode = ::GetLastError();
        std::cerr << "Error when loading DLL: " << errorCode << "\n";
        return 1;
    }

    auto pfnNVFBC_CreateEx = reinterpret_cast<NvFBCRESULT(NVFBCAPI *)(void*)>(
        ::GetProcAddress(handleNVFBC, "NvFBC_CreateEx"));
    auto pfnNVFBC_GetStatusEx = reinterpret_cast<NvFBCRESULT(NVFBCAPI *)(NvFBCStatusEx*)>(
        ::GetProcAddress(handleNVFBC, "NvFBC_GetStatusEx"));

    if (pfnNVFBC_CreateEx == nullptr || pfnNVFBC_GetStatusEx == nullptr) {
        std::cerr << "Error when resolving NvFBC exports.\n";
        ::FreeLibrary(handleNVFBC);
        return 1;
    }

    NvFBCStatusEx status = { 0 };
    NvFBCRESULT result = pfnNVFBC_GetStatusEx(&status);

    std::cout << "NVFBC Status:\n";
    std::cout << "  Capture Possible: " << status.bIsCapturePossible << "\n";
    std::cout << "  Currently Capturing: " << status.bCurrentlyCapturing << "\n";
    std::cout << "  MultiHead Support: " << status.bSupportMultiHead << "\n";
    std::cout << "Current NVFBC Version: " << status.dwNvFBCVersion << "\n";
    std::cout << "Status struct version: " << status.dwVersion << "\n";

    NvFBCCreateParams createParams = { 0 };
    createParams.dwVersion = NVFBC_CREATE_PARAMS_VER;
    createParams.dwInterfaceType = NVFBC_TO_SYS;
    createParams.dwMaxDisplayWidth = static_cast<DWORD>(-1);
    createParams.dwMaxDisplayHeight = static_cast<DWORD>(-1);
    createParams.pDevice = nullptr;
    createParams.dwInterfaceVersion = NVFBC_DLL_VERSION;

    NvFBCToSys* pNvFBCToSys = nullptr;
    result = pfnNVFBC_CreateEx(&createParams);

    if (result != NVFBC_SUCCESS || pNvFBCToSys == nullptr) {
        std::cerr << "NvFBC_CreateEx failed: " << result << "\n";
        ::FreeLibrary(handleNVFBC);
        return 1;
    }

    NVFBC_TOSYS_SETUP_PARAMS_V3 setupParams = { 0 };
    setupParams.dwVersion = NVFBC_TOSYS_SETUP_PARAMS_VER;
    setupParams.bWithHWCursor = FALSE;
    setupParams.bDiffMap = TRUE;
    setupParams.eDiffMapBlockSize = NVFBC_TOSYS_DIFFMAP_BLOCKSIZE_128X128;
    setupParams.eMode = NVFBC_TOSYS_ARGB;
    setupParams.ppBuffer = nullptr;
    setupParams.ppDiffMap = nullptr;

    result = pNvFBCToSys->NvFBCToSysSetUp(&setupParams);
    std::cout << "CreateEx result: " << result << "\n";

    ::FreeLibrary(handleNVFBC);
    return 0;
}
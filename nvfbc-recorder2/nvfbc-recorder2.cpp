#include <windows.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <string>
#include <vector>

// Include your exact local graphics headers
#include "nvFBC.h"
#include "nvFBCCuda.h"
#include "nvFBCToSys.h"
#include "nvEncodeAPI.h" // Your direct NVENC header asset
#include "NvFBCLibrary.h"
#include "Common/helper_cuda_drvapi.h"


#include "Timer.h"

#include <cuda.h>
#include <cuda_runtime_api.h>
#include "Encoder.h"




// Nastavení bufferu (např. 30 sekund při 60 FPS = 1800 snímků)
const int REPLAY_SECONDS = 30;
const int FPS = 30;
const size_t MAX_BUFFER_FRAMES = REPLAY_SECONDS * FPS;

std::deque<VideoPacket> replayBuffer;

void* create(NvFBC_CreateFunctionExType create, DWORD type, DWORD* maxWidth, DWORD* maxHeight, int adapter = 0, void* devicePtr = NULL) {
	NVFBCRESULT res = NVFBC_SUCCESS;
	NvFBCCreateParams createParams;
	memset(&createParams, 0, sizeof(createParams));

	createParams.dwVersion = NVFBC_CREATE_PARAMS_VER;
	createParams.dwInterfaceType = type;
	createParams.pDevice = devicePtr;
	createParams.dwAdapterIdx = adapter;

	res = create(&createParams);

	*maxWidth = createParams.dwMaxDisplayWidth;
	*maxHeight = createParams.dwMaxDisplayHeight;

	return createParams.pNvFBC;
}

struct RGBPixel
{
	unsigned char red;
	unsigned char green;
	unsigned char blue;
};

struct BitmapPixel
{
	unsigned char blue;
	unsigned char green;
	unsigned char red;
};

#define BITMAP_SIZE(width, height) ((((width) + 3) & ~3) * (height))
#define BITMAP_INDEX(x, y, width) (((y) * (((width) + 3) & ~3)) + (x))

bool SaveBitmap(const char* fileName, BYTE* data, int width, int height)
{
	BITMAPFILEHEADER fileHeader;
	BITMAPINFOHEADER infoHeader;
	FILE* outputFile;
	bool bRet = false;

	if (data)
	{
		if (outputFile = fopen(fileName, "wb"))
		{
			width = (width + 3) & (~3);
			int size = width * height * 3; // 24 bits per pixel

			fileHeader.bfType = 0x4D42;
			fileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + size;
			fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

			infoHeader.biSize = sizeof(BITMAPINFOHEADER);
			infoHeader.biWidth = width;
			infoHeader.biHeight = height;
			infoHeader.biPlanes = 1;
			infoHeader.biBitCount = 24;
			infoHeader.biCompression = BI_RGB;
			infoHeader.biSizeImage = BITMAP_SIZE(width, height);
			infoHeader.biXPelsPerMeter = 0;
			infoHeader.biYPelsPerMeter = 0;
			infoHeader.biClrUsed = 0;
			infoHeader.biClrImportant = 0;

			fwrite((unsigned char*)&fileHeader, 1, sizeof(BITMAPFILEHEADER), outputFile);
			fwrite((unsigned char*)&infoHeader, 1, sizeof(BITMAPINFOHEADER), outputFile);
			fwrite(data, 1, size, outputFile);

			bRet = true;
			fclose(outputFile);
		}
	}

	return bRet;
}

bool SaveRGB(const char* fileName, BYTE* data, int width, int height, int stride)
{
	bool result = false;

	RGBPixel* input = (RGBPixel*)data;
	BitmapPixel* output = new BitmapPixel[BITMAP_SIZE(width, height)];

	// Pad bytes need to be set to zero, it's easier to just set the entire chunk of memory
	memset(output, 0, BITMAP_SIZE(width, height) * sizeof(BitmapPixel));

	for (int row = 0; row < height; ++row)
	{
		for (int col = 0; col < width; ++col)
		{
			// In a bitmap (0,0) is at the bottom left, in the frame buffer it is the top left.
			int outputIdx = BITMAP_INDEX(col, row, width);
			int inputIdx = ((height - row - 1) * stride) + col;

			output[outputIdx].red = input[inputIdx].red;
			output[outputIdx].green = input[inputIdx].green;
			output[outputIdx].blue = input[inputIdx].blue;
		}
	}

	result = SaveBitmap(fileName, (BYTE*)output, width, height);

	delete[] output;

	return result;
}

int CaptureToSys() {
	HMODULE handleNVFBC = ::LoadLibraryA("C:\\Windows\\System32\\NvFBC64.dll");

	if (handleNVFBC == NULL) {
		DWORD errorCode = ::GetLastError();
		std::cerr << "Error when loading DLL: " << errorCode << "\n";
		return false;
	}

	auto pfnNVFBC_CreateEx = (NvFBC_CreateFunctionExType)GetProcAddress(handleNVFBC, "NvFBC_CreateEx");
	auto pfnNVFBC_GetStatusEx = (NvFBC_GetStatusExFunctionType)GetProcAddress(handleNVFBC, "NvFBC_GetStatusEx");

	NvFBCStatusEx status = { 0 };

	NVFBCRESULT statusResult = pfnNVFBC_GetStatusEx(&status);

	std::cout << "NVFBC Status:\n";
	std::cout << "  Capture Possible: " << status.bIsCapturePossible << "\n";
	std::cout << "  Currently Capturing: " << status.bCurrentlyCapturing << "\n";
	std::cout << "  MultiHead Support: " << status.bSupportMultiHead << "\n";
	std::cout << "Current NVFBC Version: " << status.dwNvFBCVersion << "\n";
	std::cout << "Statu struct version: " << status.dwVersion << "\n";

	if (statusResult != NVFBC_SUCCESS) {
		std::cerr << "Error getting NVFBC status: " << statusResult << "\n";
	}

	unsigned char* frameBuffer = NULL;
	NvFBCToSys* nvfbcToSys = NULL;

	NVFBCRESULT result;

	DWORD maxDisplayWidth = 0;
	DWORD maxDisplayHeight = 0;

	nvfbcToSys = (NvFBCToSys*)create(pfnNVFBC_CreateEx, NVFBC_TO_SYS, &maxDisplayWidth, &maxDisplayHeight);

	if (!nvfbcToSys)
	{
		fprintf(stderr, "Unable to create an instance of NvFBC\n");
		return -1;
	}

	NVFBC_TOSYS_SETUP_PARAMS fbcSysSetupParams = { 0 };
	fbcSysSetupParams.dwVersion = NVFBC_TOSYS_SETUP_PARAMS_VER;
	fbcSysSetupParams.eMode = NVFBCToSysBufferFormat::NVFBC_TOSYS_RGB;
	fbcSysSetupParams.bWithHWCursor = 1;
	fbcSysSetupParams.bDiffMap = FALSE;
	fbcSysSetupParams.ppBuffer = (void**)&frameBuffer;
	fbcSysSetupParams.ppDiffMap = NULL;



	result = nvfbcToSys->NvFBCToSysSetUp(&fbcSysSetupParams);

	if (result != NVFBC_SUCCESS) {
		std::cerr << "Error setting up NVFBC to system memory: " << result << "\n";
	}

	Sleep(100);
	NvFBCFrameGrabInfo grabInfo;
	NVFBC_TOSYS_GRAB_FRAME_PARAMS fbcSysGrabParams = { 0 };
	fbcSysGrabParams.dwVersion = NVFBC_TOSYS_GRAB_FRAME_PARAMS_VER;
	fbcSysGrabParams.dwFlags = NVFBC_TOSYS_NOFLAGS;
	fbcSysGrabParams.dwTargetWidth = 0;
	fbcSysGrabParams.dwTargetHeight = 0;
	fbcSysGrabParams.dwStartX = 0;
	fbcSysGrabParams.dwStartY = 0;
	fbcSysGrabParams.eGMode = NVFBCToSysGrabMode::NVFBC_TOSYS_SOURCEMODE_FULL;
	fbcSysGrabParams.pNvFBCFrameGrabInfo = &grabInfo;

	result = nvfbcToSys->NvFBCToSysGrabFrame(&fbcSysGrabParams);

	if (result == NVFBC_SUCCESS) {
		SaveRGB("out.bmp", frameBuffer, grabInfo.dwWidth, grabInfo.dwHeight, grabInfo.dwBufferWidth);
		std::cout << "Successfully saved the captured frame to out.bmp\n";
	}
	else {
		std::cerr << "Error grabbing frame: " << result << "\n";
	}

	nvfbcToSys->NvFBCToSysRelease();

	return 0;
}

std::vector<uint8_t> cursorHostBuffer(64 * 64 * 4, 0);
CUdeviceptr cursorDeviceBuffer = 0;
HCURSOR lastCursor = NULL;
int lastCurX = -1, lastCurY = -1; // Přidáno pro detekci pohybu

// Pomocná funkce: Vytáhne z Windows 32-bit obrázek aktuálního kurzoru a nasype ho do GPU
void UpdateCursorImage(HCURSOR hCursor) {
	if (!cursorDeviceBuffer) {
		cuMemAlloc(&cursorDeviceBuffer, 64 * 64 * 4); // Zabere to jen 16 KB VRAM
	}

	HDC hdc = GetDC(NULL);
	HDC memDC = CreateCompatibleDC(hdc);

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = 64;
	bmi.bmiHeader.biHeight = -64; // Top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* bits = nullptr;
	HBITMAP hBitmap = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	SelectObject(memDC, hBitmap);

	memset(bits, 0, 64 * 64 * 4); // Průhledné pozadí
	DrawIconEx(memDC, 0, 0, hCursor, 0, 0, 0, NULL, DI_NORMAL); // Vykreslí kurzor v nativní velikosti

	memcpy(cursorHostBuffer.data(), bits, 64 * 64 * 4);

	DeleteObject(hBitmap);
	DeleteDC(memDC);
	ReleaseDC(NULL, hdc);

	std::cout << "Updating cursosr with HCURSOR: " << hCursor << "\n";

	// Bleskový nahrávací přenos do grafiky
	cuMemcpyHtoD(cursorDeviceBuffer, cursorHostBuffer.data(), 64 * 64 * 4);
}

typedef NVENCSTATUS(NVENCAPI* PFN_NVENCODEAPICREATEINSTANCE)(NV_ENCODE_API_FUNCTION_LIST*);
extern "C" cudaError launch_CudaARGB2NV12Process(int srcW, int srcH, int dstW, int dstH,
	CUdeviceptr pARGBImage, CUdeviceptr pNV12Image,
	CUdeviceptr pCursor, int cursorX, int cursorY, bool drawCursor);

FILE* GetOutputFile(std::string baseName, DWORD width, DWORD height)
{
	FILE* output = NULL;

	char baseFileName[256];

	sprintf(baseFileName, "%s-%dx%d.h264", baseName.c_str(), width, height);

	output = fopen(baseFileName, "wb");

	if (NULL == output)
	{
		fprintf(stderr, "Failed to open the file %s for writing.\n", baseFileName);
		return NULL;
	}

	return output;
}
inline bool primary_context_is_active(int device_id)
{
	unsigned flags;
	int is_active;
	CUresult status = cuDevicePrimaryCtxGetState(device_id, &flags, &is_active);
	if (status != CUDA_SUCCESS) { /* error handling here */ }
	return is_active;
}

void SaveReplayBuffer() {
	if (replayBuffer.empty()) {
		std::cout << "[CHYBA] Buffer je prazdny!\n";
		return;
	}

	int keyframeCount = 0;
	int startIndex = -1;

	// Najdeme všechny klíčové snímky a určíme první čistý řezný bod
	for (size_t i = 0; i < replayBuffer.size(); ++i) {
		if (replayBuffer[i].isKeyframe) {
			keyframeCount++;
			if (startIndex == -1) {
				startIndex = i; // Zafixujeme pozici prvního klíčového snímku
			}
		}
	}

	std::cout << "\n[DEBUG DVR]\n";
	std::cout << "Celkem snimku v pameti: " << replayBuffer.size() << "\n";
	std::cout << "Pocet klicovych snimku (SPS/IDR): " << keyframeCount << "\n";

	if (startIndex == -1) {
		std::cout << "[FATAL] V bufferu neni zadny klicovy snimek! Video pravdepodobne zamrzne.\n";
		startIndex = 0; // Nouzový fallback
	}
	else {
		std::cout << "Rezu video od snimku cislo: " << startIndex << " (Zdravy zacatek)\n";
	}

	FILE* fReplay = fopen("replay_saved.h264", "wb");
	if (fReplay) {
		// Zapisujeme až od nalezeného čistého startIndexu
		for (size_t i = startIndex; i < replayBuffer.size(); ++i) {
			fwrite(replayBuffer[i].data.data(), 1, replayBuffer[i].data.size(), fReplay);
		}
		fclose(fReplay);
		std::cout << "Replay uspesne ulozen na disk!\n\n";
	}
}

DWORD default_magic[] = { 0xAEF57AC5, 0x401D1A39, 0x1B856BBE, 0x9ED0CEBA };
void* magic = default_magic;
NvU32 magic_size = sizeof(default_magic);


struct MonitorInfo {
	int left;
	int top;
};
std::vector<MonitorInfo> globalMonitors;

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
	std::vector<MonitorInfo>* monitors = (std::vector<MonitorInfo>*)dwData;
	monitors->push_back({ lprcMonitor->left, lprcMonitor->top });
	return TRUE;
}

int main() {
    // 1. INITIALIZE CUDA CONTEXT VIA WINDOWS DRIVER SYSTEM
    
	cudaDeviceProp prop;
	cudaGetDeviceProperties(&prop, 0);
	std::cout << "DEBUG: Aktivni GPU pro kernel je: " << prop.name
		<< " (Compute " << prop.major << "." << prop.minor << ")\n";

	std::cout << "ANO TOHLE JE NOVA VERZE";

	//return CaptureToSys();
	HMODULE handleNVFBC = ::LoadLibraryA("C:\\Windows\\System32\\NvFBC64.dll");
	HMODULE handleNVENC = ::LoadLibraryA("C:\\Windows\\System32\\nvEncodeAPI64.dll");

	if (handleNVFBC == NULL) {
		DWORD errorCode = ::GetLastError();
		std::cerr << "Error when loading DLL: " << errorCode << "\n";
		return false;
	}

	auto pfnNVFBC_CreateEx = (NvFBC_CreateFunctionExType)GetProcAddress(handleNVFBC, "NvFBC_CreateEx");
	auto pfnNVFBC_GetStatusEx = (NvFBC_GetStatusExFunctionType)GetProcAddress(handleNVFBC, "NvFBC_GetStatusEx");

	NvFBCStatusEx status = { 0 };

	NVFBCRESULT statusResult = pfnNVFBC_GetStatusEx(&status);

	std::cout << "NVFBC Status:\n";
	std::cout << "  Capture Possible: " << status.bIsCapturePossible << "\n";
	std::cout << "  Currently Capturing: " << status.bCurrentlyCapturing << "\n";
	std::cout << "  Can create now: " << status.bCanCreateNow<< "\n";
	std::cout << "  MultiHead Support: " << status.bSupportMultiHead << "\n";
	std::cout << "Current NVFBC Version: " << status.dwNvFBCVersion << "\n";
	std::cout << "Statu struct version: " << status.dwVersion << "\n";

	if (statusResult != NVFBC_SUCCESS) {
		std::cerr << "Error getting NVFBC status: " << statusResult << "\n";
	}

	DWORD maxDisplayWidth = 0;
	DWORD maxDisplayHeight = 0;

	globalMonitors.clear();
	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&globalMonitors);

	// Vybereme offset podle indexu, který nahráváš (měl by odpovídat createParams.dwAdapterIdx)
	int targetMonitorIndex = 0; // Změň na 0, 1, 2 podle toho, jaký monitor nahráváš
	int monitorOffsetX = 0;
	int monitorOffsetY = 0;

	if (targetMonitorIndex < globalMonitors.size()) {
		monitorOffsetX = globalMonitors[targetMonitorIndex].left;
		monitorOffsetY = globalMonitors[targetMonitorIndex].top;
		std::cout << "[INFO] Monitor offset -> X: " << monitorOffsetX << " Y: " << monitorOffsetY << "\n";
	}

	NvFBCCreateParams createParams;
	memset(&createParams, 0, sizeof(createParams));
	createParams.dwVersion = NVFBC_CREATE_PARAMS_VER;
	createParams.dwInterfaceType = NVFBC_SHARED_CUDA;
	createParams.pDevice = NULL;
	createParams.dwMaxDisplayHeight = 0;
	createParams.dwMaxDisplayWidth = 0;

	createParams.dwInterfaceVersion = 112;
	createParams.dwAdapterIdx = targetMonitorIndex;

	createParams.pPrivateData = magic;
	createParams.dwPrivateDataSize = magic_size;
	createParams.pNvFBC = NULL;

	//NvFBCCuda* pNVFBCCuda = (NvFBCCuda*)create(pfnNVFBC_CreateEx, NVFBC_SHARED_CUDA, &maxDisplayWidth, &maxDisplayHeight);
	 NVFBCRESULT createResult = pfnNVFBC_CreateEx(&createParams);

	 if (createResult != NVFBC_SUCCESS || createParams.pNvFBC == NULL) {
		 std::cerr << "Chyba vytváření NVFBC rozhraní! Výsledek: " << createResult
			 << ", Pointer: " << createParams.pNvFBC << "\n";

		 return -1;
	 }

	 NvFBCCuda* pNVFBCCuda = (NvFBCCuda*)createParams.pNvFBC;
	 maxDisplayHeight = createParams.dwMaxDisplayHeight;
	 maxDisplayWidth = createParams.dwMaxDisplayWidth;

	 CUcontext cudaContext = NULL;
	 if (createParams.cudaCtx != NULL) {
		 cudaContext = (CUcontext)createParams.cudaCtx;
	 }
	 else {
		 // Fallback: Pokud ho tam nenechalo, vytáhneme aktuální aktivní z TLS ovladače
		 cuCtxGetCurrent(&cudaContext);
	 }

	 //cuCtxPushCurrent(cudaContext);

	 std::cout << "CUDA context address: " << cudaContext << "\n";



	/*if (initResult != CUDA_SUCCESS || getDeviceResult != CUDA_SUCCESS) {
		std::cerr << "Error initializing CUDA context: "
			<< "initResult=" << initResult
			<< ", getDeviceResult=" << getDeviceResult
			<< ", createCtxResult=" << cudaContext << "\n";
		return -1;
	}*/

	DWORD maxBufferSize;
	pNVFBCCuda->NvFBCCudaGetMaxBufferSize(&maxBufferSize);

	std::cout << "Max buffer size for CUDA capture: " << maxBufferSize << " bytes\n";

	CUdeviceptr argbbuffer;
	CUresult argbBufferResult = cuMemAlloc(&argbbuffer, maxBufferSize);

	std::vector<CUdeviceptr> nv12Buffers(MAX_BUF_QUEUE);
	size_t nv12Size = createParams.dwMaxDisplayWidth * createParams.dwMaxDisplayHeight * 1.5;

	for (int i = 0; i < MAX_BUF_QUEUE; i++) {
		cuMemAlloc(&nv12Buffers[i], nv12Size);
	}
	

	if (argbBufferResult != CUDA_SUCCESS) {
		std::cerr << "Error allocating CUDA buffers: "
			<< "ARGB buffer result=" << argbBufferResult
			<< ", NV12 buffer result=" << "\n";
		return -1;
	}

	NVFBC_CUDA_SETUP_PARAMS setupParams = { 0 };
	setupParams.dwVersion = NVFBC_CUDA_SETUP_PARAMS_VER;
	setupParams.eFormat = NVFBC_TOCUDA_ARGB;
	setupParams.bEnableSeparateCursorCapture = false;

	NVFBCRESULT setupResult = pNVFBCCuda->NvFBCCudaSetup(&setupParams);

	if (setupResult != NVFBC_SUCCESS) {
		std::cerr << "Error setting up CUDA capture format: " << setupResult << "\n";
		return -1;
	}

	std::cout << "CUDA Capture setup successful.\n";

	std::cout << "Allocated CUDA buffer of size: " << maxBufferSize << " bytes\n";

	// Variable allocation for capture loop

	FILE* fout = NULL;

	Timer grabTimer;
	Timer frameTimer;

	DWORD currentWidth = 0, currentHeight = 0;


	bool isRecording = true;
	uint64_t frameCnt = 0;
	Encoder encoder;

	if (S_OK != encoder.Init(cudaContext, maxBufferSize))
	{
		fprintf(stderr, "Failed to initialize encoder\n");
		return -1;
	}

	frameTimer.reset();
	auto next_frame_time = std::chrono::steady_clock::now();
	const auto frame_duration = std::chrono::microseconds(1000000 / FPS); // U tebe např. fps = 60


		while (isRecording) {
			grabTimer.reset();

			if (GetAsyncKeyState(VK_F10) & 0x8000) {
				std::cout << "\n[F10] zachyceno! Ukládám...\n";
				SaveReplayBuffer();
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				isRecording = false;
				break;
			}

			NvFBCFrameGrabInfo frameGrabInfo;
			NVFBC_CUDA_GRAB_FRAME_PARAMS grabParams = { 0 };
			grabParams.dwVersion = NVFBC_CUDA_GRAB_FRAME_PARAMS_VER;
			grabParams.dwFlags = 0;

			grabParams.pNvFBCFrameGrabInfo = &frameGrabInfo;
			grabParams.pCUDADeviceBuffer = (void*)argbbuffer;

			NVFBCRESULT grabResult = pNVFBCCuda->NvFBCCudaGrabFrame(&grabParams);

			if (grabResult == NVFBCRESULT::NVFBC_SUCCESS) {
				auto now = std::chrono::steady_clock::now();
				int frames_to_generate = 1;

				std::cout << "Captured frame " << frameCnt << " at " << frameGrabInfo.dwWidth << "x" << frameGrabInfo.dwHeight
					<< ", buffer size: " << frameGrabInfo.dwBufferWidth << " bytes\n";
		
				if (now > next_frame_time) {
					auto missed_time = std::chrono::duration_cast<std::chrono::microseconds>(now - next_frame_time);
					int missed_frames = missed_time.count() / frame_duration.count();

					if (missed_frames > FPS * 2) {
						missed_frames = FPS * 2; // Omezíme doplňování na max 2 sekundy
						next_frame_time = now;   // Tvrdý reset časovače do přítomnosti
					}
					else {
						next_frame_time += std::chrono::microseconds(missed_frames * frame_duration.count());
					}
					frames_to_generate += missed_frames;
				}

				int targetW = 1920;
				int targetH = 1080;
				if (frameCnt == 0) {
					HRESULT encoderSetupResult = encoder.SetupEncoder(targetW, targetH, targetW, 25000000);
					if (encoderSetupResult != S_OK) {
						fprintf(stderr, "Failed to setup encoder\n");
						return -1;
					}
					currentWidth = frameGrabInfo.dwWidth;
					currentHeight = frameGrabInfo.dwHeight;
				}

				CURSORINFO ci = { sizeof(CURSORINFO) };
				bool drawCursor = false;
				int curX = 0, curY = 0;

				if (GetCursorInfo(&ci) && ci.flags == CURSOR_SHOWING) {
					drawCursor = true;

					// Získání přesné špičky kurzoru (Hotspot = hrot šipky)
					ICONINFO iconInfo;
					if (GetIconInfo(ci.hCursor, &iconInfo)) {
						curX = ci.ptScreenPos.x - iconInfo.xHotspot - monitorOffsetX;
						curY = ci.ptScreenPos.y - iconInfo.yHotspot - monitorOffsetY;
						DeleteObject(iconInfo.hbmColor);
						DeleteObject(iconInfo.hbmMask);
					}
					else {
						curX = ci.ptScreenPos.x - monitorOffsetX;
						curY = ci.ptScreenPos.y - monitorOffsetY;
					}

					// Pokud se tvar kurzoru změnil (např. ze šipky na ručičku nad odkazem), updatneme ho v GPU
					if (ci.hCursor != lastCursor) {
						UpdateCursorImage(ci.hCursor);
						lastCursor = ci.hCursor;
					}
				}


				unsigned int currentSlot = frameCnt % MAX_BUF_QUEUE;
				cudaError_t kernelErr = launch_CudaARGB2NV12Process(
					frameGrabInfo.dwWidth, frameGrabInfo.dwHeight,
					targetW, targetH,
					argbbuffer, nv12Buffers[currentSlot], cursorDeviceBuffer, curX, curY, drawCursor
				);
				cuStreamSynchronize(NULL);

				for (int f = 0; f < frames_to_generate; f++) {
					unsigned int encodeSlot = frameCnt % MAX_BUF_QUEUE;

					

					if (encodeSlot != currentSlot) {
						std::cout << "Replaying skipped frames.";
						cuMemcpyDtoD(nv12Buffers[encodeSlot], nv12Buffers[currentSlot], nv12Size);
					}

					if (S_OK != encoder.LaunchEncode(encodeSlot, nv12Buffers[encodeSlot])) {
						fprintf(stderr, "Failed encoding frame %d\n", frameCnt);
					}

					VideoPacket packet;
					if (encoder.GetBitstreamData(encodeSlot, packet)) {
						replayBuffer.push_back(packet);
						if (replayBuffer.size() > MAX_BUFFER_FRAMES) {
							replayBuffer.pop_front();
						}
					}
					frameCnt++;
				}

				// Konečně posuneme časovač na další budoucí snímek a uspíme smyčku
				next_frame_time += frame_duration;
				pNVFBCCuda->NvFBCCudaGPUBasedCPUSleep(frame_duration.count());

			}
			else {
				fprintf(stderr, "Grab frame failed. NvFBCCudaGrabFrame returned: %d\n", grabResult);
				return -1;
			}
		}

	encoder.TearDown();
	if (NULL != fout)
		fclose(fout);

	pNVFBCCuda->NvFBCCudaRelease();
	return 0;
}


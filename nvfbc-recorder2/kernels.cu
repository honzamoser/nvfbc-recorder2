#include <assert.h>
#pragma comment(lib, "cuda.lib")
#include <cuda.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include "Common/helper_math.h"

__global__ void CudaARGB2NV12BilinearKernel(const unsigned char* pARGB, unsigned char* pNV12, int srcW, int srcH, int dstW, int dstH,
    const unsigned char* pCursor, int cursorX, int cursorY, bool drawCursor)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= dstW || y >= dstH) return;

    // Pøesný výpoèet fyzického støedu pixelu pro hladký pøechod
    float srcX = ((x + 0.5f) * (float)srcW / (float)dstW) - 0.5f;
    float srcY = ((y + 0.5f) * (float)srcH / (float)dstH) - 0.5f;

    // Najdeme 4 nejbližší pixely kolem našeho bodu
    int x1 = max(0, (int)floorf(srcX));
    int y1 = max(0, (int)floorf(srcY));
    int x2 = min(x1 + 1, srcW - 1);
    int y2 = min(y1 + 1, srcH - 1);

    // Spoèítáme si "váhu" neboli jak blízko k danému pixelu jsme
    float dx = srcX - floorf(srcX);
    float dy = srcY - floorf(srcY);

    // Vypoèítáme pamìové indexy (ARGB má 4 bajty na pixel)
    int p11 = (y1 * srcW + x1) * 4;
    int p12 = (y1 * srcW + x2) * 4;
    int p21 = (y2 * srcW + x1) * 4;
    int p22 = (y2 * srcW + x2) * 4;

    // Makro pro smíchání barvy ze 4 pixelù do jednoho krásného prùmìru
#define BILINEAR(channel) \
        ( (1.0f - dx) * (1.0f - dy) * pARGB[p11 + channel] + \
          (dx)        * (1.0f - dy) * pARGB[p12 + channel] + \
          (1.0f - dx) * (dy)        * pARGB[p21 + channel] + \
          (dx)        * (dy)        * pARGB[p22 + channel] )

    float b = BILINEAR(0);
    float g = BILINEAR(1);
    float r = BILINEAR(2);

    if (drawCursor) {
        // Pøepoèet souøadnic kurzoru z 1440p do zmenšeného 1080p videa
        int scaleCX = (cursorX * dstW) / srcW;
        int scaleCY = (cursorY * dstH) / srcH;

        int cx = x - scaleCX;
        int cy = y - scaleCY;

        // Zasahuje tento pixel do našeho 64x64 kurzoru?
        if (cx >= 0 && cx < 64 && cy >= 0 && cy < 64) {
            int cIdx = (cy * 64 + cx) * 4;
            float cB = pCursor[cIdx];
            float cG = pCursor[cIdx + 1];
            float cR = pCursor[cIdx + 2];
            float cA = pCursor[cIdx + 3] / 255.0f; // Prùhlednost (Alpha)

            if (cA > 0.01f) {
                // Pøimícháme kurzor k barvám, které jsme vyèetli z plochy
                r = (cR * cA) + (r * (1.0f - cA));
                g = (cG * cA) + (g * (1.0f - cA));
                b = (cB * cA) + (b * (1.0f - cA));
            }
        }
    }

    // Zápis Y složky (jasová / èernobílá)
    pNV12[y * dstW + x] = (unsigned char)(((66 * (int)r + 129 * (int)g + 25 * (int)b + 128) >> 8) + 16);

    // Zápis UV složky (barvy, ukládáme pouze pro každý sudý pixel)
    if (x % 2 == 0 && y % 2 == 0) {
        unsigned char u = (unsigned char)(((-38 * (int)r - 74 * (int)g + 112 * (int)b + 128) >> 8) + 128);
        unsigned char v = (unsigned char)(((112 * (int)r - 94 * (int)g - 18 * (int)b + 128) >> 8) + 128);

        int uvIdx = (dstW * dstH) + (y / 2) * dstW + x;
        pNV12[uvIdx] = u;
        pNV12[uvIdx + 1] = v;
    }
}

// Spouštìè zùstává pojmenovaný stejnì, takže v main.cpp NEMUSÍŠ mìnit ani èárku!
extern "C" cudaError launch_CudaARGB2NV12Process(int srcW, int srcH, int dstW, int dstH, CUdeviceptr pARGBImage, CUdeviceptr pNV12Image, CUdeviceptr pCursor, int cursorX, int cursorY, bool drawCursor)
{
    dim3 block(32, 8);
    dim3 grid((dstW + block.x - 1) / block.x, (dstH + block.y - 1) / block.y);

    // Spustíme nový bilineární kernel
    CudaARGB2NV12BilinearKernel <<< grid, block >>> ((const unsigned char*)pARGBImage, (unsigned char*)pNV12Image, srcW, srcH, dstW, dstH, (const unsigned char*)pCursor, cursorX, cursorY, drawCursor);

    return cudaGetLastError();
}
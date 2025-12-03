// OSFG Optical Flow Compute Shader
// Simple block-matching optical flow for Phase 1 proof of concept
// MIT License - Part of Open Source Frame Generation project

// Input textures
Texture2D<float4> g_CurrentFrame : register(t0);
Texture2D<float4> g_PreviousFrame : register(t1);

// Output texture (motion vectors)
RWTexture2D<int2> g_MotionVectors : register(u0);

// Sampler
SamplerState g_LinearSampler : register(s0);

// Constants
cbuffer OpticalFlowConstants : register(b0)
{
    uint2 g_InputSize;        // Input frame dimensions
    uint2 g_OutputSize;       // Motion vector texture dimensions
    uint  g_BlockSize;        // Block size for matching (typically 8)
    uint  g_SearchRadius;     // Search radius in pixels
    float g_MinLuminance;     // Min luminance for normalization
    float g_MaxLuminance;     // Max luminance for normalization
};

// Convert RGB to luminance
float RGBToLuminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Compute Sum of Absolute Differences (SAD) between two blocks
float ComputeBlockSAD(int2 currentBlockPos, int2 previousBlockPos)
{
    float sad = 0.0;

    [unroll]
    for (int y = 0; y < 8; y++)
    {
        [unroll]
        for (int x = 0; x < 8; x++)
        {
            int2 currentPos = currentBlockPos + int2(x, y);
            int2 previousPos = previousBlockPos + int2(x, y);

            // Clamp to image bounds
            currentPos = clamp(currentPos, int2(0, 0), int2(g_InputSize) - 1);
            previousPos = clamp(previousPos, int2(0, 0), int2(g_InputSize) - 1);

            float currentLum = RGBToLuminance(g_CurrentFrame[currentPos].rgb);
            float previousLum = RGBToLuminance(g_PreviousFrame[previousPos].rgb);

            sad += abs(currentLum - previousLum);
        }
    }

    return sad;
}

// Main compute shader - one thread per block
[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Check bounds
    if (dispatchThreadId.x >= g_OutputSize.x || dispatchThreadId.y >= g_OutputSize.y)
        return;

    // Current block position in input image
    int2 blockPos = int2(dispatchThreadId.xy) * g_BlockSize;

    // Initialize best match
    float bestSAD = 1e10;
    int2 bestMotion = int2(0, 0);

    // Search in a window around the current block position
    int searchRadius = (int)g_SearchRadius;

    for (int dy = -searchRadius; dy <= searchRadius; dy++)
    {
        for (int dx = -searchRadius; dx <= searchRadius; dx++)
        {
            int2 searchPos = blockPos + int2(dx, dy);

            // Skip if outside image
            if (searchPos.x < 0 || searchPos.y < 0 ||
                searchPos.x + g_BlockSize > g_InputSize.x ||
                searchPos.y + g_BlockSize > g_InputSize.y)
                continue;

            float sad = ComputeBlockSAD(blockPos, searchPos);

            if (sad < bestSAD)
            {
                bestSAD = sad;
                bestMotion = int2(dx, dy);
            }
        }
    }

    // Store motion vector (scaled to fixed-point for R16G16_SINT)
    // Motion is stored as pixels * 16 for sub-pixel precision later
    g_MotionVectors[dispatchThreadId.xy] = bestMotion * 16;
}

// Raymarching compute shader

[[vk::binding(0, 0)]]
RWTexture2D<float4> image;

// Push constants block
struct ComputePushConstants
{
    float4 data1; // .x = time
    float4 data2; // .xy = mouse position (normalized), .zw = mouse click state
    float4 data3;
    float4 data4;
};

[[vk::push_constant]]
ComputePushConstants pushConstants;

// Torus signed distance function
float sdTorus(float3 p, float2 t)
{
    float2 q = float2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

// 2D rotation matrix
float2x2 rot2D(float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2x2(c, -s, s, c);
}

// Cosine based palette
float3 palette(float t)
{
    float3 a = float3(0.0, 0.0, 0.0);
    float3 b = float3(0.749, 0.761, 0.059);
    float3 c = float3(1.0, 1.0, 1.0);
    float3 d = float3(-0.430, -0.397, -0.083);
    return a + b * cos(6.283185 * (c * t + d));
}

float map(float3 p, float iTime)
{
    p.z += iTime * 0.4;

    // Space repetition
    p.xy = (frac(p.xy) - 0.5);
    p.z = fmod(p.z, 0.25) - 0.125;

    float torus = sdTorus(p * float3(1, 1, 0.6), float2(0.1, 0.1)); // torus sdf

    return torus;
}

float4 mainImage(float2 fragCoord, float2 iResolution, float iTime, float4 iMouse)
{
    // Normalized pixel coordinates (0,1) and making the center of the canvas as (0,0),
    // so the coordinates became (-1,1) and also adjusting for the aspect ratio of the canvas.
    float2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    float2 m = (iMouse.xy * 2.0 - iResolution.xy) / iResolution.y;
    float fov = 1.5; // var to adjust field of view

    float3 ro = float3(0, 0, -3); // ray origin
    float3 rd = normalize(float3(uv * fov, 1)); // ray direction
    float t = 0.0; // total distance travelled

    float3 col = float3(0); // final pixel color

    if (iMouse.z < 0.0)
        m = float2(cos(iTime * 0.2), sin(iTime * 0.2));

    // Raymarching
    int i;
    for (i = 0; i < 80; i++)
    {
        float3 p = ro + rd * t; // position along the ray

        p.xy = mul(rot2D(t * 0.2 * m.x), p.xy);

        p.y += sin(t * (m.y + 1.0) * 0.5) * 0.35; // wiggle ray

        float d = map(p, iTime); // current distance to the scene

        t += d; // march the ray

        if (d < 0.001 || t > 100.0)
            break;
    }

    col = palette(t * 0.04 + float(i) * 0.005);

    // Output to screen
    return float4(col, 1.0);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 globalID : SV_DispatchThreadID)
{
    int2 texelCoord = int2(globalID.xy);
    int2 size;
    image.GetDimensions(size.x, size.y);

    if (texelCoord.x < size.x && texelCoord.y < size.y)
    {
        float iTime = pushConstants.data1.x;
        float2 iResolution = float2(size);
        float4 iMouse = float4(pushConstants.data2.xy * iResolution, pushConstants.data2.zw);

        float4 color = mainImage(float2(texelCoord), iResolution, iTime, iMouse);
        image[texelCoord] = color;
    }
}

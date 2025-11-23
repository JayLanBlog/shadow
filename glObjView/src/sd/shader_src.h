#pragma once

namespace sp
{
// =============================================================
// Shader 1: 基础物体绘制 (支持纯色覆盖，用于生成影子的黑底)
// =============================================================
const char* baseVs = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 model;
uniform mat4 projection;

void main() {
    gl_Position = projection * model * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";


const char* baseFs = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D image;
uniform vec4 colorMod; // (1,1,1,1)为原图, (0,0,0,1)为纯黑

void main() {
    vec4 texColor = texture(image, TexCoord);
    if(texColor.a < 0.1) discard; // 简单的 Alpha Test
    FragColor = texColor * colorMod; 
}
)";

// =============================================================
// Shader 2: Kawase Blur (专用于 FBO 处理)
// 利用硬件线性插值，每次采样4个点
// =============================================================

const char* kawaseVs = R"(
#version 330 core
// 全屏三角形优化：不需要 VBO，直接根据 gl_VertexID 生成坐标
// 减少了 CPU 到 GPU 的数据传输和绑定
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)";

const char* kawaseFs = R"(
#version 330 core
out vec4 FragColor;

uniform sampler2D screenTexture;
uniform vec2 texSize; // 纹理尺寸
uniform float offset; // 扩散步长

void main() {
    // 计算当前像素的 UV
    vec2 uv = gl_FragCoord.xy / texSize;
    
    // Kawase 核心：采样四个角落
    // 由于我们使用 GL_LINEAR，采样点位于像素之间时会自动混合
    vec2 halfPixel = (vec2(offset + 0.5) / texSize);

    vec4 color = vec4(0.0);
    color += texture(screenTexture, uv + vec2(halfPixel.x, halfPixel.y));
    color += texture(screenTexture, uv + vec2(-halfPixel.x, halfPixel.y));
    color += texture(screenTexture, uv + vec2(-halfPixel.x, -halfPixel.y));
    color += texture(screenTexture, uv + vec2(halfPixel.x, -halfPixel.y));

    FragColor = color * 0.25;
}
)";
} // namespace sp

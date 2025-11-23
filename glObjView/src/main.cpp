#include <GL/glew.h>
#include <SDL/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// 窗口设置
const int SCREEN_WIDTH = 1920;
const int SCREEN_HEIGHT = 1080;

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

// ----------------工具函数封装----------------

GLuint CompileShader(const char* vsCode, const char* fsCode) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsCode, nullptr);
    glCompileShader(vs);
    // (此处省略了错误检查代码，实际开发请加上)

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsCode, nullptr);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

GLuint LoadTexture(const char* path, int* w, int* h) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    int ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, w, h, &ch, 0);
    if (data) {
        GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, *w, *h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        // 关键：软阴影需要线性插值
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(data);
    }
    return id;
}

// FBO 管理器结构体 (支持 Ping-Pong)
struct PingPongFBO {
    GLuint fbo[2];
    GLuint tex[2];
    int width, height;

    void Init(int w, int h) {
        width = w; height = h;
        glGenFramebuffers(2, fbo);
        glGenTextures(2, tex);

        for (int i = 0; i < 2; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
            glBindTexture(GL_TEXTURE_2D, tex[i]);
            // 使用 RGBA16F 或 RGBA 均可
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[i], 0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Cleanup() {
        glDeleteFramebuffers(2, fbo);
        glDeleteTextures(2, tex);
    }
};

// 全局变量
GLuint quadVAO, quadVBO;
GLuint emptyVAO; // 用于全屏三角形绘制

void InitQuad() {
    float vertices[] = {
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f
    };
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // 空 VAO，用于 drawArrays 绘制全屏三角形
    glGenVertexArrays(1, &emptyVAO);
}

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    // 设置 GL 3.3 Core
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow("Kawase Soft Shadow", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    glewExperimental = GL_TRUE;
    glewInit();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 1. 初始化资源
    InitQuad();
    GLuint baseProg = CompileShader(baseVs, baseFs);
    GLuint kawaseProg = CompileShader(kawaseVs, kawaseFs);

    int imgW, imgH;
    GLuint spriteTex = LoadTexture("2.png", &imgW, &imgH);
    if (imgW == 0) { imgW = 100; imgH = 100; } // Fallback

    // 2. 初始化 Kawase FBO (尺寸为原图的 1/4，加速且增加模糊度)
    PingPongFBO ppFBO;
    int shadowW = imgW / 2;  // 越小越模糊，也越快
    int shadowH = imgH / 2;
    ppFBO.Init(shadowW, shadowH);

    bool running = true;
    SDL_Event evt;

    glm::mat4 projection = glm::ortho(0.0f, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT, 0.0f);

    while (running) {
        while (SDL_PollEvent(&evt)) {
            if (evt.type == SDL_QUIT) running = false;
        }

        // ====================================================
        // 步骤 1: 生成阴影遮罩 (Pass 0) -> 绘制到 FBO[0]
        // ====================================================
        glBindFramebuffer(GL_FRAMEBUFFER, ppFBO.fbo[0]);
        glViewport(0, 0, shadowW, shadowH);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(baseProg);
        // 使用 0~1 正交投影填充整个 FBO
        glm::mat4 fboProj = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(baseProg, "projection"), 1, GL_FALSE, &fboProj[0][0]);
        glm::mat4 model = glm::mat4(1.0f);
        glUniformMatrix4fv(glGetUniformLocation(baseProg, "model"), 1, GL_FALSE, &model[0][0]);
        // 变黑
        glUniform4f(glGetUniformLocation(baseProg, "colorMod"), 0.0f, 0.0f, 0.0f, 1.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, spriteTex);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // ====================================================
        // 步骤 2: Kawase Blur 迭代 (Pass 1 ~ N)
        // ====================================================
        glUseProgram(kawaseProg);
        glUniform2f(glGetUniformLocation(kawaseProg, "texSize"), (float)shadowW, (float)shadowH);
        glBindVertexArray(emptyVAO); // 切换到空 VAO 进行全屏三角形绘制

        bool horizontal = true;
        bool first_iteration = true;
        int amount = 3; // 模糊迭代次数，次数越多越柔和，消耗越大

        for (int i = 0; i < amount; i++)
        {
            // 设置 offset: Kawase 模糊核随着迭代次数向外扩散 (0, 1, 2, 3...)
            glUniform1f(glGetUniformLocation(kawaseProg, "offset"), (float)(i + 1));

            // 绑定目标 FBO (Ping-Pong)
            // i % 2 == 0: 从 fbo[0] 读，写到 fbo[1]
            // i % 2 == 1: 从 fbo[1] 读，写到 fbo[0]
            glBindFramebuffer(GL_FRAMEBUFFER, ppFBO.fbo[(i + 1) % 2]);
            glBindTexture(GL_TEXTURE_2D, ppFBO.tex[i % 2]); // 读上一张

            // 绘制全屏三角形 (3个顶点) - 极度高效
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        // 循环结束时，最后的结果存储在 ppFBO.tex[amount % 2] 中

        // ====================================================
        // 步骤 3: 渲染到屏幕 (Pass Final)
        // ====================================================
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        glClearColor(0.6f, 0.7f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(baseProg);
        glUniformMatrix4fv(glGetUniformLocation(baseProg, "projection"), 1, GL_FALSE, &fboProj[0][0]);

        glm::vec2 pos(300, 200);

        // A. 绘制影子 (使用最后一次模糊的纹理)
        glm::mat4 shadowModel = glm::mat4(1.0f);

        // 1. 平移 + 偏移 (影子在右下)
        shadowModel = glm::translate(shadowModel, glm::vec3(pos.x + 40.0f, pos.y + (imgH * 0.9f), 0.0f));

        // 2. 错切矩阵 (Shear)
        // 手动构造：x' = x + 1.0 * y (向右歪)
        glm::mat4 shear(1.0f);
        shear[1][0] = 1.2f; // Shear X factor
        shadowModel = shadowModel * shear;

        // 3. 缩放与压扁 (Stretch & Squash)
        // 注意：我们把影子压扁到原来的 0.4 倍高度，看起来像投射在地面
        // 宽度保持 imgW，但因为使用了 Kawase 模糊的小纹理，放大绘制会自动产生柔和边缘
        shadowModel = glm::scale(shadowModel, glm::vec3((float)imgW, (float)imgH * 0.5f, 1.0f));

        // 影子原点修正：通常希望以底部中心或左下角变换，这里简单修正回中心点
        // 由于 Quad 是 0,0 到 1,1，左上对齐。加上 Shear 后需要微调位置。

        glUniformMatrix4fv(glGetUniformLocation(baseProg, "model"), 1, GL_FALSE, &model[0][0]);
        glUniform4f(glGetUniformLocation(baseProg, "colorMod"), 0.0f, 0.0f, 0.0f, 0.5f); // 纯黑, alpha 0.5

        // 绑定模糊后的最终结果
        glBindTexture(GL_TEXTURE_2D, ppFBO.tex[amount % 2]);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // B. 绘制本体
        glm::mat4 spriteModel = glm::mat4(1.0f);
        spriteModel = glm::translate(spriteModel, glm::vec3(pos.x, pos.y, 0.0f));
        spriteModel = glm::scale(spriteModel, glm::vec3((float)imgW, (float)imgH, 1.0f));

        glUniformMatrix4fv(glGetUniformLocation(baseProg, "model"), 1, GL_FALSE, &spriteModel[0][0]);
        glUniform4f(glGetUniformLocation(baseProg, "colorMod"), 1.0f, 1.0f, 1.0f, 1.0f); // 恢复原色

        glBindTexture(GL_TEXTURE_2D, spriteTex);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        SDL_GL_SwapWindow(window);
    }

    ppFBO.Cleanup();
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteVertexArrays(1, &emptyVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteProgram(baseProg);
    glDeleteProgram(kawaseProg);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
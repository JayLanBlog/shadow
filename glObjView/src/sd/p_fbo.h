#pragma once
#include <GL/glew.h>

namespace sp {
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
}
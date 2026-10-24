// Sample both production shader recipes through an RGBA32F framebuffer.
#include "boat_vessel_wake.h"
#if !defined(__EMSCRIPTEN__)
#include "external/glad.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL GPU %d: %s\n", __LINE__, #x);                                    \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

bool boat_vessel_wake_shadow_read(Texture2D texture, float *pixels)
{
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, pixels);
    return glGetError() == GL_NO_ERROR;
}

void boat_vessel_wake_gpu_check(BoatVesselWake wake)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "Ferry wake production recipe parity");
    CHECK(IsWindowReady());
    const char *paths[]
        = { "assets/shaders/waterfront_water.vs", "assets/shaders/waterfront_water.fs" };
    const char *vertex = "#version 330\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                         "gl_Position=vec4(2*p-1,0,1);}";
    GLuint texture, framebuffer, vao;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, NULL);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glViewport(0, 0, 1, 1);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    for (unsigned stage = 0; stage < 2; ++stage) {
        char *source = LoadFileText(paths[stage]);
        CHECK(source);
        char *begin = strstr(source, "// BEGIN VESSEL RECIPE");
        char *end = strstr(source, "// END VESSEL RECIPE");
        CHECK(begin && end && end > begin);
        size_t capacity = (size_t)(end - begin) + 256;
        char *code = malloc(capacity);
        CHECK(code);
        int written = snprintf(code, capacity,
            "#version 330\nout vec4 color; uniform vec2 probe;\n"
            "%.*s\nvoid main(){color=vesselWake(probe);}",
            (int)(end - begin), begin);
        CHECK(written > 0 && (size_t)written < capacity);
        Shader shader = LoadShaderFromMemory(vertex, code);
        CHECK(shader.id && shader.id != rlGetShaderIdDefault());
        free(code);
        UnloadFileText(source);
        glUseProgram(shader.id);
        double error[4] = { 0 }, scale[4] = { 0 };
        float maximum = 0;
        for (int t = -13; t <= 53; t += 4) {
            float uniforms[12];
            boat_vessel_wake_uniforms(wake, t, uniforms);
            glUniform4fv(GetShaderLocation(shader, "vesselOrigin"), 1, uniforms);
            glUniform4fv(GetShaderLocation(shader, "vesselMotion"), 1, uniforms + 4);
            glUniform4fv(GetShaderLocation(shader, "vesselShape"), 1, uniforms + 8);
            for (int z = -510; z <= -180; z += 21)
                for (int x = -270; x <= 100; x += 19) {
                    Vector2 p = { x + .173f, z + .391f };
                    BoatVesselWakeSample sample = boat_vessel_wake_query(wake, p, t);
                    float reference[4] = { sample.height, -sample.gradient.x, sample.gradient.y,
                        sample.vertical_velocity };
                    glUniform2f(GetShaderLocation(shader, "probe"), -p.x, p.y);
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                    float result[4];
                    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, result);
                    for (int j = 0; j < 4; ++j) {
                        CHECK(isfinite(result[j]));
                        float delta = result[j] - reference[j];
                        maximum = fmaxf(maximum, fabsf(delta));
                        error[j] += delta * delta;
                        scale[j] += reference[j] * reference[j];
                    }
                }
        }
        printf("vessel %s max_abs=%.8g relative_L2=", stage ? "FS" : "VS", maximum);
        for (int j = 0; j < 4; ++j) {
            CHECK(scale[j] > 1);
            double relative = sqrt(error[j] / scale[j]);
            printf(" %.8g", relative);
            CHECK(relative < .0001);
        }
        puts("");
        CHECK(maximum < .0002f);
        UnloadShader(shader);
    }
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    glDeleteVertexArrays(1, &vao);
    CloseWindow();
    puts("boat_vessel_gpu=ok");
}
#else
bool boat_vessel_wake_shadow_read(Texture2D texture, float *pixels)
{
    (void)texture;
    (void)pixels;
    return false;
}
void boat_vessel_wake_gpu_check(BoatVesselWake wake) { (void)wake; }
#endif

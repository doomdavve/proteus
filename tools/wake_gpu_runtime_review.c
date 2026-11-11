/* Native GL 3.3 fragment-pass feasibility prototype. Not a gameplay backend. */
#include "core/boat_wake.h"
#include "external/glad.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G 9.81
#define WAKE_PI 3.14159265358979323846
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
enum { W = 192, H = 128, N = W * H, ORDER = 256 };
typedef struct {
    GLuint texture, framebuffer;
    int w, h;
} Target;
static Shader shader;
static int mode_loc, coefficient_loc, scale_loc, channel_loc, dt_loc, size_loc, query_x_loc;
static Target state[2], scratch[3], result, source_result, source, terrain, neighbors[4], queries,
    energy, reduced[8], visual;
static double coefficients[ORDER];
static double impulse[N], rates[N];
static unsigned char land[N];
static float upload[4 * N];
static unsigned current, geometry;
static const char *vertex
    = "#version 330\n"
      "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(2*p-1,0,1);}";
static const char *fragment
    = "#version 330\n"
      "uniform sampler2D t0,t1,t2,t3,n0,n1,n2,n3;\n"
      "uniform int mode,channel,query_x; uniform float coefficient,scale,dt; uniform ivec2 size;\n"
      "out vec4 color;\n"
      "float value(float id){int i=int(id);return texelFetch(t0,ivec2(i%192,i/192),0).r;}\n"
      "float difference(ivec2 p,vec4 ids){float v=texelFetch(t0,p,0).r;"
      "return 4*v-((value(ids.x)+value(ids.y))+(value(ids.z)+value(ids.w)));}\n"
      "float lap(ivec2 p){return (8.0/5)*difference(p,texelFetch(n0,p,0))"
      "-(1.0/5)*difference(p,texelFetch(n1,p,0))"
      "+(8.0/315)*difference(p,texelFetch(n2,p,0))"
      "-(1.0/560)*difference(p,texelFetch(n3,p,0));}\n"
      "void main(){ivec2 p=ivec2(gl_FragCoord.xy);\n"
      "if(mode==0){color=vec4(coefficient*texelFetch(t0,p,0)[channel],0,0,0);}\n"
      "else if(mode==1){float v=scale*texelFetch(t0,p,0).r-texelFetch(t1,p,0).r;"
      "v+=coefficient*texelFetch(t2,p,0)[channel];v-=scale*(2.0/"
      "13.003174603174603)*lap(p);color=vec4(v,0,0,0);}\n"
      "else if(mode==2){color=vec4((2.0/13.003174603174603)*lap(p),0,0,0);}\n"
      "else if(mode==3){vec4 s=texelFetch(t2,p,0);float d=.5*texelFetch(t0,p,0).r;"
      "s.a+=1000*.75*.75*(s.g+.5*d)*.5*texelFetch(t1,p,0).r;s.g+=d;color=s;}\n"
      "else if(mode==4){vec4 s=texelFetch(t0,p,0);s.g-=.5*dt*9.81*s.r;color=s;}\n"
      "else if(mode==5){vec4 s=texelFetch(t1,p,0);s.r+=dt*texelFetch(t0,p,0).r;"
      "s.g-=.5*dt*9.81*s.r;float d=texelFetch(t3,p,0).r;"
      "s.b+=.5*1000*.75*.75*9.81*s.r*s.r*(-d)*(2+d);s.r*=1+d;color=s;}\n"
      "else if(mode==6){ivec2 q=ivec2(query_x+p.x,60);vec4 s=texelFetch(t0,q,0);"
      "vec2 dx=(texelFetch(t0,q+ivec2(1,0),0).rg-texelFetch(t0,q-ivec2(1,0),0).rg)/1.5;"
      "vec2 dz=(texelFetch(t0,q+ivec2(0,1),0).rg-texelFetch(t0,q-ivec2(0,1),0).rg)/1.5;"
      "color=p.y==0?vec4(s.r,texelFetch(t1,q,0).r,dx.r,dz.r):vec4(s.g,dx.g,dz.g,0);}\n"
      "else if(mode==7){color=vec4(0);for(int y=0;y<2;y++)for(int x=0;x<2;x++){"
      "ivec2 q=2*p+ivec2(x,y);if(all(lessThan(q,size)))color+=texelFetch(t0,q,0);}}\n"
      "else if(mode==8){vec4 s=texelFetch(t0,p,0);color=vec4(.5*1000*.75*.75*"
      "(9.81*s.r*s.r+s.g*texelFetch(t1,p,0).r),s.b,s.a,0);}\n"
      "else {vec2 uv=gl_FragCoord.xy/vec2(320,200);float eta=1000*texture(t0,uv).r;"
      "color=vec4(.1+eta,.3+eta,.6+eta,1);}}\n";

static Target target(int w, int h, const float *data)
{
    Target t = { .w = w, .h = h };
    glGenTextures(1, &t.texture);
    glBindTexture(GL_TEXTURE_2D, t.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &t.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, t.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    if (!data) {
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    return t;
}

static void pass(int mode, Target out, Target a, Target b, Target c, Target d)
{
    Target inputs[] = { a, b, c, d };
    for (unsigned i = 0; i < 4; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, inputs[i].texture);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, out.framebuffer);
    glViewport(0, 0, out.w, out.h);
    glUniform1i(mode_loc, mode);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void apply(Target input, int channel, Target output)
{
    glUniform1i(channel_loc, channel);
    glUniform1f(coefficient_loc, (float)coefficients[ORDER - 1]);
    pass(0, scratch[0], input, input, input, input);
    glBindFramebuffer(GL_FRAMEBUFFER, scratch[1].framebuffer);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned a = 0, previous = 1, next = 2;
    for (int j = ORDER - 2; j >= 0; --j) {
        glUniform1f(coefficient_loc, (float)coefficients[j]);
        glUniform1f(scale_loc, j ? 2 : 1);
        pass(1, scratch[next], scratch[a], scratch[previous], input, input);
        unsigned old = previous;
        previous = a;
        a = next;
        next = old;
    }
    pass(2, output, scratch[a], input, input, input);
}

static void prepare(unsigned kind, double dt)
{
    geometry = kind;
    glUniform1i(query_x_loc, kind ? 80 : 16);
    for (unsigned z = 0; z < H; ++z)
        for (unsigned x = 0; x < W; ++x) {
            unsigned i = z * W + x;
            land[i] = kind == 1 ? (x == W / 2 && z > 40 && z < 88) : kind == 2 ? x == W / 2 : 0;
            double edge = fmin(fmin(x, W - 1 - x), fmin(z, H - 1 - z));
            rates[i] = edge < 14 ? 2 * pow((14 - edge) / 14, 2) : 0;
            upload[4 * i] = (float)expm1(-rates[i] * dt);
            upload[4 * i + 1] = (float)land[i];
            upload[4 * i + 2] = upload[4 * i + 3] = 0;
        }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, terrain.texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_FLOAT, upload);
    for (unsigned r = 1; r <= 4; ++r) {
        for (unsigned z = 0; z < H; ++z)
            for (unsigned x = 0; x < W; ++x) {
                unsigned i = z * W + x;
                for (unsigned axis = 0; axis < 2; ++axis) {
                    int pos = (int)(axis ? z : x), start = pos, end = pos + 1,
                        length = axis ? H : W;
                    if (!land[i]) {
                        while (start > 0
                            && !land[axis ? (unsigned)(start - 1) * W + x
                                          : z * W + (unsigned)(start - 1)])
                            --start;
                        while (end < length
                            && !land[axis ? (unsigned)end * W + x : z * W + (unsigned)end])
                            ++end;
                    }
                    int count = end - start;
                    for (unsigned sign = 0; sign < 2; ++sign) {
                        int offset = pos - start + (sign ? 1 : -1) * (int)r;
                        offset = ((offset % (2 * count)) + 2 * count) % (2 * count);
                        if (offset >= count)
                            offset = 2 * count - 1 - offset;
                        unsigned id = land[i] ? i
                            : axis            ? (unsigned)(start + offset) * W + x
                                              : z * W + (unsigned)(start + offset);
                        upload[4 * i + 2 * axis + sign] = (float)id;
                    }
                }
            }
        glActiveTexture(GL_TEXTURE4 + r - 1);
        glBindTexture(GL_TEXTURE_2D, neighbors[r - 1].texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_FLOAT, upload);
    }
    for (unsigned i = 0; i < 2; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, state[i].framebuffer);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    current = 0;
    glUniform1f(dt_loc, (float)dt);
}

static void make_source(unsigned step, double dt)
{
    double center = (geometry ? 62 : 12) + 2 * sin(.7 * (step + .5) * dt);
    for (unsigned z = 0; z < H; ++z)
        for (unsigned x = 0; x < W; ++x) {
            unsigned i = z * W + x;
            double dx = (x + .5) * .75 - center, dz = (z + .5) * .75 - 48;
            double r2 = dx * dx + dz * dz;
            /* Float input deliberately shared with CPU oracle. */
            float value = land[i] || r2 >= 81
                ? 0
                : (float)(dt * .02 * dx * exp(-r2 / 18) * pow(1 - r2 / 81, 2));
            impulse[i] = value;
            upload[4 * i] = value;
            upload[4 * i + 1] = upload[4 * i + 2] = upload[4 * i + 3] = 0;
        }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source.texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_FLOAT, upload);
}

static void update(void)
{
    apply(source, 0, source_result);
    pass(3, state[1 - current], source, source_result, state[current], terrain);
    current = 1 - current;
    pass(4, state[1 - current], state[current], result, source, terrain);
    current = 1 - current;
    apply(state[current], 1, result);
    pass(5, state[1 - current], result, state[current], source, terrain);
    current = 1 - current;
    pass(3, state[1 - current], source, source_result, state[current], terrain);
    current = 1 - current;
}

static void read_queries(float values[48], float ledger[4])
{
    apply(state[current], 1, result);
    pass(6, queries, state[current], result, source, terrain);
    glReadPixels(0, 0, 6, 2, GL_RGBA, GL_FLOAT, values);
    pass(8, energy, state[current], result, source, terrain);
    Target input = energy;
    for (unsigned i = 0; i < 8; ++i) {
        glUniform2i(size_loc, input.w, input.h);
        pass(7, reduced[i], input, result, source, terrain);
        input = reduced[i];
    }
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, ledger);
    pass(9, visual, state[current], result, source, terrain);
    glFinish(); /* Include rendering consumption in elapsed wall time. */
}

static void release(Target t)
{
    glDeleteTextures(1, &t.texture);
    glDeleteFramebuffers(1, &t.framebuffer);
}

int main(int argc, char **argv)
{
    const char *capture = argc == 3 && strcmp(argv[1], "--capture") == 0 ? argv[2] : NULL;
    bool check_only = capture || (argc == 2 && strcmp(argv[1], "--check") == 0);
    if (argc > 1 && !check_only)
        return 2;
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "wake GPU runtime checkpoint");
    REQUIRE(IsWindowReady());
    shader = LoadShaderFromMemory(vertex, fragment);
    REQUIRE(IsShaderValid(shader) && shader.id != 0);
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glUseProgram(shader.id);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0, 0, 0, 0);
    mode_loc = GetShaderLocation(shader, "mode");
    coefficient_loc = GetShaderLocation(shader, "coefficient");
    scale_loc = GetShaderLocation(shader, "scale");
    channel_loc = GetShaderLocation(shader, "channel");
    dt_loc = GetShaderLocation(shader, "dt");
    size_loc = GetShaderLocation(shader, "size");
    query_x_loc = GetShaderLocation(shader, "query_x");
    const char *names[] = { "t0", "t1", "t2", "t3", "n0", "n1", "n2", "n3" };
    for (int i = 0; i < 8; ++i)
        glUniform1i(GetShaderLocation(shader, names[i]), i);
    for (unsigned i = 0; i < 2; ++i)
        state[i] = target(W, H, NULL);
    for (unsigned i = 0; i < 3; ++i)
        scratch[i] = target(W, H, NULL);
    result = target(W, H, NULL);
    source_result = target(W, H, NULL);
    source = target(W, H, NULL);
    terrain = target(W, H, NULL);
    for (unsigned i = 0; i < 4; ++i)
        neighbors[i] = target(W, H, NULL);
    queries = target(6, 2, NULL);
    energy = target(W, H, NULL);
    visual = target(320, 200, NULL);
    int w = W, h = H;
    for (unsigned i = 0; i < 8; ++i) {
        w = (w + 1) / 2;
        h = (h + 1) / 2;
        reduced[i] = target(w, h, NULL);
    }
    double bound = 13.003174603174603, scale = 4 * sqrt(bound) / (WAKE_PI * .75);
    double tail = scale / (2 * (2.0 * ORDER + 1)), sum = 0, moment = 0;
    for (unsigned j = ORDER; j > 0; --j) {
        double fejer = 2.0 * (ORDER + 1 - j) / ((ORDER + 1.0) * (ORDER + 1.0));
        double weight = scale / (4.0 * j * j - 1) + tail * fejer;
        sum += weight;
        moment += j * weight;
        coefficients[j - 1] = j == 1 ? moment : 2 * (moment - (j - 1) * sum);
    }
    printf("{\n\"device\": \"%s\",\n\"validation\": [\n", glGetString(GL_RENDERER));
    bool passed = true;
    for (unsigned kind = 0; kind < 3; ++kind) {
        double dt = 1.0 / 60;
        prepare(kind, dt);
        BoatWakeConfig c = { W, H, .75, dt, 1000, ORDER, land, rates, 8, false };
        BoatWake *cpu = boat_wake_create(&c);
        REQUIRE(cpu);
        for (unsigned step = 0; step < 32; ++step) {
            make_source(step, dt);
            update();
            for (unsigned i = 0; i < N; ++i)
                impulse[i] *= .5;
            REQUIRE(boat_wake_kick(cpu, impulse) && boat_wake_step(cpu)
                && boat_wake_kick(cpu, impulse));
        }
        glBindFramebuffer(GL_FRAMEBUFFER, state[current].framebuffer);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, upload);
        double err = 0, norm = 0, wall_max = 0;
        for (unsigned i = 0; i < N; ++i) {
            double eta, psi;
            REQUIRE(boat_wake_sample(cpu, i % W, i / W, &eta, &psi));
            err += pow(upload[4 * i] - eta, 2) + pow(upload[4 * i + 1] - psi, 2);
            norm += eta * eta + psi * psi;
            if (kind == 2 && i % W > W / 2)
                wall_max = fmax(wall_max, fabs(upload[4 * i]) + fabs(upload[4 * i + 1]));
        }
        float values[48], ledger[4];
        read_queries(values, ledger);
        if (capture && kind == 1) {
            unsigned char *pixels = MemAlloc(320 * 200 * 4);
            REQUIRE(pixels);
            glReadPixels(0, 0, 320, 200, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            Image image = { .data = pixels,
                .width = 320,
                .height = 200,
                .mipmaps = 1,
                .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            ImageFlipVertical(&image);
            REQUIRE(ExportImage(image, capture));
            UnloadImage(image);
        }
        BoatWakeLedger l = boat_wake_ledger(cpu);
        double energy_error = fabs(ledger[0] - l.retained) / l.retained;
        double work_error = fabs(ledger[2] - l.injected) / l.injected;
        double sink_error = fabs(ledger[1] - l.absorbed) / fmax(1e-30, l.absorbed);
        /* Independent A psi oracle: a zero-height CPU owner drifts by dt A psi. */
        for (unsigned i = 0; i < N; ++i) {
            double eta;
            REQUIRE(boat_wake_sample(cpu, i % W, i / W, &eta, &impulse[i]));
        }
        c.absorption = NULL;
        BoatWake *velocity = boat_wake_create(&c);
        REQUIRE(velocity && boat_wake_kick(velocity, impulse) && boat_wake_step(velocity));
        double query_error = 0, query_norm = 0;
        unsigned query_x = kind ? 80 : 16;
        for (unsigned i = 0; i < 6; ++i) {
            double e, p;
            REQUIRE(boat_wake_sample(cpu, query_x + i, 60, &e, &p));
            double ve, vp, ex0, px0, ex1, px1, ez0, pz0, ez1, pz1;
            REQUIRE(boat_wake_sample(velocity, query_x + i, 60, &ve, &vp));
            REQUIRE(boat_wake_sample(cpu, query_x - 1 + i, 60, &ex0, &px0));
            REQUIRE(boat_wake_sample(cpu, query_x + 1 + i, 60, &ex1, &px1));
            REQUIRE(boat_wake_sample(cpu, query_x + i, 59, &ez0, &pz0));
            REQUIRE(boat_wake_sample(cpu, query_x + i, 61, &ez1, &pz1));
            double expected[8] = { e, ve / dt, (ex1 - ex0) / 1.5, (ez1 - ez0) / 1.5, p,
                (px1 - px0) / 1.5, (pz1 - pz0) / 1.5, 0 };
            for (unsigned j = 0; j < 8; ++j) {
                double actual = values[4 * i + (j < 4 ? j : 24 + j - 4)];
                query_error += pow(actual - expected[j], 2);
                query_norm += expected[j] * expected[j];
            }
        }
        boat_wake_destroy(velocity);
        query_error = sqrt(query_error / fmax(1e-30, query_norm));
        double relative = sqrt(err / norm);
        bool valid = isfinite(relative) && relative < .005 && energy_error < .005
            && work_error < .005
            && fabs(ledger[1] - l.absorbed) < .005 * l.absorbed + 1e-6 * fabs(l.injected)
            && wall_max == 0 && query_error < .005;
        passed &= valid;
        printf("%s{\"geometry\": %u, \"state_l2\": %.9g, \"retained_relative\": %.9g, "
               "\"work_relative\": %.9g, \"sink_relative\": %.9g, \"query_l2\": %.9g, "
               "\"wall_max\": %.9g, \"passed\": %s}",
            kind ? ",\n" : "", kind, relative, energy_error, work_error, sink_error, query_error,
            wall_max, valid ? "true" : "false");
        boat_wake_destroy(cpu);
    }
    puts("\n],\n\"workload\": [");
    if (!check_only)
        for (unsigned hz = 30; hz <= 120; hz *= 2) {
            double dt = 1.0 / hz;
            prepare(0, dt); /* Same open rectangle as the cosine CPU comparison below. */
            float values[48], ledger[4];
            for (unsigned step = 0; step < 4; ++step) {
                make_source(step, dt);
                update();
                read_queries(values, ledger);
            }
            double start = GetTime();
            for (unsigned step = 0; step < 12; ++step) {
                make_source(step + 4, dt);
                update();
                read_queries(values, ledger);
            }
            double ms = 1000 * (GetTime() - start) / 12;
            printf("%s{\"hz\": %u, \"source_step_queries_ledger_render_ms\": %.6f, "
                   "\"ms_per_60hz_frame\": %.6f, \"finite\": %s}",
                hz == 30 ? "" : ",\n", hz, ms, ms * hz / 60,
                isfinite(ledger[0]) ? "true" : "false");
            passed &= isfinite(ledger[0]);
            BoatWakeConfig c = { W, H, .75, dt, 1000, ORDER, NULL, rates, 8, false };
            BoatWake *cpu = boat_wake_create(&c);
            REQUIRE(cpu && boat_wake_use_cosine(cpu));
            boat_wake_prepare(cpu);
            double cpu_start = 0, checksum = 0;
            for (unsigned step = 0; step < 16; ++step) {
                if (step == 4)
                    cpu_start = GetTime();
                make_source(step, dt); /* Includes the same upload; conservative for CPU. */
                REQUIRE(boat_wake_step_with_impulse(cpu, impulse));
                boat_wake_prepare(cpu);
                for (unsigned i = 0; i < 6; ++i) {
                    BoatWakeSample q;
                    REQUIRE(
                        boat_wake_query(cpu, (16 + i + .5) * .75 - 72, (60 + .5) * .75 - 48, &q));
                    checksum += q.height + q.vertical_velocity + q.velocity_x + q.velocity_z;
                }
                BoatWakeLedger audit = boat_wake_ledger(cpu);
                REQUIRE(isfinite(audit.retained));
                for (unsigned i = 0; i < N; ++i) {
                    double eta, psi;
                    REQUIRE(boat_wake_sample(cpu, i % W, i / W, &eta, &psi));
                    upload[4 * i] = (float)eta;
                    upload[4 * i + 1] = (float)psi;
                }
                glBindTexture(GL_TEXTURE_2D, state[0].texture);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_FLOAT, upload);
                pass(9, visual, state[0], state[0], state[0], state[0]);
                glFinish();
            }
            double cpu_ms = 1000 * (GetTime() - cpu_start) / 12;
            printf(",\n{\"hz\": %u, \"backend\": \"cosine_cpu\", "
                   "\"source_step_queries_ledger_upload_render_ms\": %.6f, \"ms_per_60hz_frame\": "
                   "%.6f}",
                hz, cpu_ms, cpu_ms * hz / 60);
            REQUIRE(isfinite(checksum));
            boat_wake_destroy(cpu);
        }
    printf("\n],\n\"passed\": %s\n}\n", passed ? "true" : "false");
    REQUIRE(glGetError() == GL_NO_ERROR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    for (unsigned i = 0; i < 2; ++i)
        release(state[i]);
    for (unsigned i = 0; i < 3; ++i)
        release(scratch[i]);
    release(result);
    release(source_result);
    release(source);
    release(terrain);
    release(queries);
    release(energy);
    release(visual);
    for (unsigned i = 0; i < 4; ++i)
        release(neighbors[i]);
    for (unsigned i = 0; i < 8; ++i)
        release(reduced[i]);
    glDeleteVertexArrays(1, &vao);
    UnloadShader(shader);
    CloseWindow();
    return passed ? 0 : 1;
}

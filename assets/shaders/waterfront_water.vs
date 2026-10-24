#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform int physicalWaves;
uniform float wakeAmplitude;
uniform vec2 wakeOffset; // World X/Z translation of the scripted source.
uniform int playerWakeCount;
uniform vec4 playerWakeOrigins[64];
uniform vec4 playerWakeParameters[64];
uniform vec4 waveParameters[3]; // World X/Z wave vector, amplitude, CPU-reduced phase.
uniform float crossingWakeTime; // Gated scripted clock; -1 after its finite support.
uniform int waterPatchPass; // 0: legacy, 1: planar far field, 2: clipped near patch.
uniform vec4 wavePatch; // Boat world X/Z, full displacement radius, zero displacement radius.

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragWorldPosition;
out vec3 fragWorldNormal;


// Same bounded packet and derivatives as boat_water.c, in reflected world X/Z.
// BEGIN STORM RECIPE
uniform vec4 stormArea; // World centre X/Z, radius, height.
uniform vec4 stormPhase; // CPU-reduced phases for the four directional wave trains.
uniform vec4 stormMotion; // World velocity X/Z, CPU-reduced long-bend phase, reserved.
vec4 stormWave(vec2 world)
{
    if (stormArea.z <= 0.0 || stormArea.w <= 0.0) return vec4(0.0);
    vec2 p = world - stormArea.xy;
    float r2 = dot(p,p)/(stormArea.z*stormArea.z);
    if (r2 >= 1.0) return vec4(0.0);
    float edge = 1.0-r2, envelope = edge*edge*edge;
    vec2 envelopeGradient = -6.0*edge*edge*p/(stormArea.z*stormArea.z);
    vec2 waves[4] = vec2[4](vec2(-0.27,-0.32),vec2(-0.38,-0.26),
        vec2(-0.16,-0.30),vec2(-0.47,-0.18));
    float speeds[4] = float[4](3.2,3.75,2.45,4.15);
    float amplitudes[4] = float[4](0.8,0.4,0.22,0.12);
    float offsets[4] = float[4](0.0,1.1,2.7,4.2);
    vec2 bendWave = vec2(-0.013,0.017);
    float bendPhase = dot(p,bendWave)-stormMotion.z;
    vec4 result = vec4(0.0);
    for (int i=0; i<4; ++i) {
        float bend = bendPhase + offsets[i]*0.73;
        float bendSlope = 0.85*cos(bend);
        float phase = dot(p,waves[i])+0.85*sin(bend)+offsets[i]-stormPhase[i];
        float a = stormArea.w*amplitudes[i];
        float h = a*(sin(phase)+0.12*sin(2.0*phase+0.4));
        float slope = a*(cos(phase)+0.24*cos(2.0*phase+0.4));
        vec2 phaseGradient = waves[i]+bendWave*bendSlope;
        result += vec4(envelope*h, envelopeGradient*h+phaseGradient*(envelope*slope),
            -envelope*slope*(speeds[i]+0.37*bendSlope));
    }
    result.w -= dot(result.yz, stormMotion.xy);
    return result;
}
// END STORM RECIPE

// BEGIN VESSEL RECIPE
uniform vec4 vesselOrigin; // world X/Z, unit direction X/Z
uniform vec4 vesselMotion; // speed, height, duration, lifetime
uniform vec4 vesselShape; // bounded clock (-1 inactive), wavelength, spread, half width
vec4 vesselWake(vec2 world)
{
    if (vesselShape.x < 0.0 || vesselMotion.y <= 0.0) return vec4(0.0);
    vec2 p = world - vesselOrigin.xy;
    vec2 side = vec2(-vesselOrigin.w, vesselOrigin.z);
    float emitted = dot(p, vesselOrigin.zw) / vesselMotion.x;
    float age = vesselShape.x - emitted;
    if (emitted <= 0.0 || emitted >= vesselMotion.z || age <= 0.0 || age >= vesselMotion.w)
        return vec4(0.0);
    float transverse = dot(p, side);
    float radius = sqrt(transverse * transverse + 4.0);
    float q = radius - vesselShape.z * age;
    if (abs(q) >= vesselShape.w) return vec4(0.0);
    float pi = 3.14159265359, k = 2.0 * pi / vesselShape.y;
    float width2 = vesselShape.w * vesselShape.w;
    float edge = 1.0 - q*q / width2;
    float f = sin(k*q) * edge*edge*edge;
    float df = k*cos(k*q) * edge*edge*edge - sin(k*q)*6.0*q/width2*edge*edge;
    float window = pow(sin(pi*emitted/vesselMotion.z), 2.0);
    float envelope = pow(sin(pi*age/vesselMotion.w), 2.0);
    float dw = pi/vesselMotion.z * sin(2.0*pi*emitted/vesselMotion.z);
    float de = pi/vesselMotion.w * sin(2.0*pi*age/vesselMotion.w);
    float along = ((dw*envelope-window*de)*f + window*envelope*df*vesselShape.z)
        / vesselMotion.x;
    float across = window*envelope*df*transverse/radius;
    return vesselMotion.y * vec4(window*envelope*f,
        vesselOrigin.zw*along + side*across, window*(de*f-envelope*df*vesselShape.z));
}
// END VESSEL RECIPE

vec3 crossingWake(vec2 world, float clock)
{
    world -= wakeOffset;
    float emitted = (-world.x + 24.0) / 4.0;
    float age = clock - emitted;
    if (wakeAmplitude == 0.0 || emitted <= 0.0 || emitted >= 12.0 || age <= 0.0 || age >= 12.0)
        return vec3(0.0);
    float radius = sqrt((world.y + 8.0) * (world.y + 8.0) + 0.25);
    float q = radius - 2.4 * age;
    if (abs(q) >= 5.0) return vec3(0.0);
    float pi = 3.14159265359, k = 2.0 * pi / 6.0;
    float edge = 1.0 - q * q / 25.0;
    float f = sin(k * q) * edge * edge * edge;
    float df = k * cos(k * q) * edge * edge * edge
        - sin(k * q) * 6.0 * q / 25.0 * edge * edge;
    float window = pow(sin(pi * emitted / 12.0), 2.0);
    float envelope = pow(sin(pi * age / 12.0), 2.0);
    float dw = pi / 12.0 * sin(2.0 * pi * emitted / 12.0);
    float de = pi / 12.0 * sin(2.0 * pi * age / 12.0);
    return wakeAmplitude * vec3(window * envelope * f,
        -((dw * envelope - window * de) * f + window * envelope * df * 2.4) / 4.0,
        window * envelope * df * (world.y + 8.0) / radius);
}


// BEGIN OBJECT RECIPE -- same signed ridge and derivatives as boat_wake_objects.c.
uniform int objectWakeEnabled;
uniform float objectWakeLife;
uniform int objectWakePlayerCount;
uniform float objectWakeTrafficLife;
uniform sampler2D objectWakeData;
vec3 objectWakePiece(vec2 p, vec4 a, vec4 b, float life)
{
    vec2 delta=p-a.xy, direction=a.zw;
    float u=dot(delta,direction);
    if (abs(u)>=b.x) return vec3(0.0);
    vec2 normal=vec2(-direction.y,direction.x);
    float v=dot(delta,normal), radius=sqrt(v*v+0.25);
    float clock=b.y, age=clock-u*b.w;
    float q=radius-0.9-2.4*age;
    if (q<=-7.0 || q>=2.0) return vec3(0.0);
    float inverseWidth=0.25+0.25*step(0.0,q);
    float crestEdge=max(0.0,1.0-q*q*inverseWidth*inverseWidth);
    float troughQ=q+4.5;
    float troughEdge=max(0.0,1.0-troughQ*troughQ/6.25);
    float shape=crestEdge*crestEdge*crestEdge-0.28*troughEdge*troughEdge*troughEdge;
    float shapeSlope=-6.0*q*inverseWidth*inverseWidth*crestEdge*crestEdge
        +0.28*6.0*troughQ/6.25*troughEdge*troughEdge;
    float riseT=clamp(age/0.65,0.0,1.0);
    float fallT=clamp((age-(0.75*life-b.x*b.w))/(0.25*life),0.0,1.0);
    float rise=riseT*riseT*(3.0-2.0*riseT);
    float fall=1.0-fallT*fallT*(3.0-2.0*fallT);
    float birth=smoothstep(0.0,0.3,clock);
    float envelope=birth*rise*fall;
    float envelopeAge=birth*(6.0*riseT*(1.0-riseT)/0.65*fall
        -rise*6.0*fallT*(1.0-fallT)/(0.25*life));
    float angle=3.14159265359*u/(2.0*b.x), c=cos(angle);
    float window=c*c, windowU=-3.14159265359/b.x*c*sin(angle);
    vec2 qG=normal*(v/radius)+direction*(2.4*b.w);
    vec2 gradient=envelope*(direction*(windowU*shape)+qG*(window*shapeSlope))
        -direction*(b.w*window*shape*envelopeAge);
    return b.z*vec3(envelope*window*shape,gradient);
}
vec3 objectWake(vec2 p)
{
    vec3 result=vec3(0.0);
    for (int i=0; i<256; ++i) {
        if (i>=playerWakeCount) break;
        result+=objectWakePiece(p,texelFetch(objectWakeData,ivec2(i,0),0),
            texelFetch(objectWakeData,ivec2(i,1),0),
            i>=objectWakePlayerCount && objectWakeTrafficLife>0.0 ? objectWakeTrafficLife : objectWakeLife);
    }
    return result;
}
// END OBJECT RECIPE

// Same owned path history and analytic derivatives as boat_water.c. Each entry is
// two vec4s: world centre/direction, support half-length/age at frame time/amplitude/
// inverse speed. Packing age rather than absolute birth keeps long sessions precise.
vec3 playerWake(vec2 p)
{
    if (objectWakeEnabled != 0) return objectWake(p);
    float sum = 0.0, weight = 0.0;
    vec2 sumG = vec2(0.0), weightG = vec2(0.0);
    for (int i = 0; i < playerWakeCount; ++i) {
        vec4 a = playerWakeOrigins[i], b = playerWakeParameters[i];
        vec2 delta = p - a.xy, direction = a.zw;
        vec2 normal = vec2(-direction.y, direction.x);
        float u = dot(delta, direction);
        if (abs(u) >= b.x) continue;
        float age = b.y - u * b.w;
        if (age <= 0.4 || age >= 20.0) continue;
        float v = dot(delta, normal), radius = sqrt(v * v + 0.25);
        float q = radius - 0.9 - 1.6 * age;
        if (abs(q) >= 4.5) continue;
        float l = 1.0 - u * u / (b.x * b.x), r = 1.0 - q * q / 20.25;
        float wl = l * l * l, wq = r * r * r;
        float dl = -6.0 * u / (b.x * b.x) * l * l, dq = -6.0 * q / 20.25 * r * r;
        vec2 ageG = -direction * b.w;
        vec2 qG = normal * (v / radius) - 1.6 * ageG;
        float w = wl * wq;
        vec2 wg = direction * (dl * wq) + qG * (wl * dq);
        float riseT = clamp((age - 0.4), 0.0, 1.0), fallT = clamp((age - 14.0) / 6.0, 0.0, 1.0);
        float rise = riseT * riseT * (3.0 - 2.0 * riseT);
        float fall = 1.0 - fallT * fallT * (3.0 - 2.0 * fallT);
        float de = 6.0 * riseT * (1.0 - riseT) * fall - rise * fallT * (1.0 - fallT);
        float envelope = rise * fall, k = 6.28318530718 / 6.0;
        // Phase speed 3 m/s; the packet envelope spreads at 1.6 m/s.
        float phase = q - 1.4 * age;
        vec2 phaseG = qG - 1.4 * ageG;
        float wave = sin(k * phase), slope = k * cos(k * phase);
        float value = b.z * envelope * wave;
        vec2 valueG = b.z * (ageG * (de * wave) + phaseG * (envelope * slope));
        sum += w * value;
        sumG += wg * value + valueG * w;
        weight += w;
        weightG += wg;
    }
    float divisor = sqrt(1.0 + weight * weight);
    return vec3(sum, sumG - sum * weight / (1.0 + weight * weight) * weightG) / divisor;
}

// BEGIN NATIVE WAKE FIELD
// Shared degree-eleven tensor interpolation; keep VS/FS and boat_wake_query in sync.
uniform int wakeFieldEnabled;
uniform sampler2D wakeField;
uniform vec3 wakeGrid; // cells X/Z, spacing in metres; fixed centred simulation domain.
int wakeMirror(int i, int n) {
    i = ((i % (2*n)) + 2*n) % (2*n);
    return i >= n ? 2*n-1-i : i;
}
void wakeWeights(float t, out float w[12], out float d[12]) {
    const float inverse[12] = float[12](-2.505210838544172e-08, 2.7557319223985888e-07, -1.3778659611992946e-06, 4.1335978835978839e-06, -8.2671957671957678e-06, 1.1574074074074073e-05, -1.1574074074074073e-05, 8.2671957671957678e-06, -4.1335978835978839e-06, 1.3778659611992946e-06, -2.7557319223985888e-07, 2.505210838544172e-08);
    float prefix[13],suffix[13],dp[13],ds[13];
    prefix[0]=1.0; suffix[12]=1.0; dp[0]=0.0; ds[12]=0.0;
    for (int k=0;k<12;++k) {
        float v=t-float(k-5);
        prefix[k+1]=prefix[k]*v; dp[k+1]=dp[k]*v+prefix[k];
        int j=11-k; v=t-float(j-5);
        suffix[j]=suffix[j+1]*v; ds[j]=ds[j+1]*v+suffix[j+1];
    }
    for (int k=0;k<12;++k) {
        w[k]=prefix[k]*suffix[k+1]*inverse[k];
        d[k]=(dp[k]*suffix[k+1]+prefix[k]*ds[k+1])*inverse[k];
    }
}
vec3 wakeFieldSample(vec2 world) {
    if (wakeFieldEnabled == 0) return vec3(0.0);
    vec2 grid = vec2(-world.x,world.y)/wakeGrid.z+wakeGrid.xy*0.5-0.5;
    if (any(lessThan(grid,vec2(-0.5))) || any(greaterThan(grid,wakeGrid.xy-0.5)))
        return vec3(0.0);
    ivec2 base = ivec2(floor(grid));
    float wx[12],wz[12],gx[12],gz[12];
    wakeWeights(fract(grid.x),wx,gx); wakeWeights(fract(grid.y),wz,gz);
    vec3 q = vec3(0.0);
    for (int j = 0; j < 12; ++j) for (int k = 0; k < 12; ++k) {
        ivec2 cell = ivec2(wakeMirror(base.x+k-5,int(wakeGrid.x)),
            wakeMirror(base.y+j-5,int(wakeGrid.y)));
        float eta = texelFetch(wakeField,cell,0).r;
        q += eta*vec3(wx[k]*wz[j],-gx[k]*wz[j]/wakeGrid.z,wx[k]*gz[j]/wakeGrid.z);
    }
    return q;
}

float wakeVertexHeight(vec2 world) {
    if (wakeFieldEnabled == 0) return 0.0;
    vec2 grid = vec2(-world.x,world.y)/wakeGrid.z+wakeGrid.xy*0.5-0.5;
    if (all(lessThan(abs(grid-round(grid)),vec2(0.0001))))
        return texelFetch(wakeField,ivec2(round(grid)),0).r;
    return wakeFieldSample(world).x;
}

// END NATIVE WAKE FIELD
void main()
{
    vec3 position = vertexPosition;
    if (physicalWaves != 0 && waterPatchPass != 1) {
        position.y = 0.0;
        for (int i = 0; i < 3; ++i) {
            vec4 w = waveParameters[i];
            position.y += w.z * sin(dot(w.xy, position.xz) - w.w);
        }
        position.y += crossingWake(position.xz, crossingWakeTime).x + playerWake(position.xz).x
            + wakeVertexHeight(position.xz) + vesselWake(position.xz).x + stormWave(position.xz).x;
        if (waterPatchPass == 2 && wakeFieldEnabled == 0)
            position.y *= 1.0 - smoothstep(wavePatch.z, wavePatch.w,
                length(position.xz - wavePatch.xy));
    }
    vec4 worldPosition = matModel * vec4(position, 1.0);
    fragTexCoord = vertexTexCoord;
    // Both representations use the same boating albedo. The legacy city water is brighter.
    fragColor = physicalWaves != 0 && waterPatchPass != 0
        ? vec4(45.0, 79.0, 104.0, 255.0) / 255.0 : vertexColor;
    fragWorldPosition = worldPosition.xyz;
    fragWorldNormal = physicalWaves != 0 ? vec3(0.0, 1.0, 0.0)
        : normalize((matNormal * vec4(vertexNormal, 0.0)).xyz);
    gl_Position = mvp * vec4(position, 1.0);
}

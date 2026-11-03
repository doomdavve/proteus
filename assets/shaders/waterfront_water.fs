#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragWorldPosition;
in vec3 fragWorldNormal;

uniform sampler2D texture0;
uniform sampler2D reflectionTexture;
uniform vec4 objectColor;
uniform mat4 reflectionViewProjection;
uniform vec3 cameraPosition;
// Explicit float payloads: each periodic component is reduced from double world time on
// the CPU. There is no shared float time uniform.
uniform float crossingWakeTime; // Gated scripted clock; -1 after its finite support.
uniform vec2 detailRipplePhase; // Long and short exposed-water detail ripples.
uniform vec4 reflectionRipplePhase; // Four timeline reflection ripple components.
uniform vec3 sunDirection;
// The frame's light: body and foam multiplier, brightness left in sun shadow, glitter colour
// (zero hides it) and specular highlight multiplier. Daytime is 1, 0.55, warm white and 1.
uniform vec3 waterLight;
uniform float waterShadowFloor;
uniform vec3 glintColor;
uniform vec3 highlightTint;
// Glitter character: sun road strength (0 by day) and the share of sparkles kept.
uniform float glintSheen;
uniform float glintSparkle;
uniform int physicalWaves;
uniform float wakeAmplitude;
uniform vec2 wakeOffset; // World X/Z translation of the scripted source.
uniform int playerWakeCount;
uniform vec4 playerWakeOrigins[64];
uniform vec4 playerWakeParameters[64];
uniform vec4 waveParameters[3]; // World X/Z wave vector, amplitude, CPU-reduced phase.
uniform int reflectionsEnabled;
uniform int shadowsEnabled;
uniform int farShadowsEnabled;
uniform sampler2D farShadowMap;
uniform mat4 farLightViewProjection;
uniform sampler2D shadowMap;
uniform mat4 lightViewProjection;
uniform int waterPatchPass;
uniform vec4 wavePatch;
uniform int waterfrontWater; // Only playable mapped water; leave basin materials alone.
uniform float surfacePixelScale;

out vec4 finalColor;

// Square light-space coverage; blend before the detailed map ends, then fade the far map.
float shadow_visibility(float bias)
{
    vec4 nearLight = lightViewProjection * vec4(fragWorldPosition, 1.0);
    vec3 p = nearLight.xyz / nearLight.w * 0.5 + 0.5;
    float edge = max(abs(p.x * 2.0 - 1.0), abs(p.y * 2.0 - 1.0));
    float nearVisibility = 1.0;
    if (all(greaterThan(p, vec3(0.0))) && all(lessThan(p, vec3(1.0))))
        nearVisibility = p.z - bias > texture(shadowMap, p.xy).r ? 0.0 : 1.0;
    if (farShadowsEnabled == 0)
        return nearVisibility;

    float nearWeight = 1.0 - smoothstep(0.70, 0.95, edge);
    if (nearWeight >= 1.0)
        return nearVisibility;
    vec4 farLight = farLightViewProjection * vec4(fragWorldPosition, 1.0);
    vec3 q = farLight.xyz / farLight.w * 0.5 + 0.5;
    float farVisibility = 1.0;
    if (all(greaterThan(q, vec3(0.0))) && all(lessThan(q, vec3(1.0)))) {
        // Match the near map's world-space bias across the longer 2200 m depth span.
        farVisibility = q.z - bias * (1400.0 / 2200.0) > texture(farShadowMap, q.xy).r
            ? 0.0 : 1.0;
        float farEdge = max(abs(q.x * 2.0 - 1.0), abs(q.y * 2.0 - 1.0));
        farVisibility = mix(farVisibility, 1.0, smoothstep(0.80, 0.98, farEdge));
    }
    return mix(farVisibility, nearVisibility, nearWeight);
}

float waterShadow()
{
    if (shadowsEnabled == 0) return 1.0;
    return mix(waterShadowFloor, 1.0, shadow_visibility(0.00018));
}

// G4: the bound map's baked water atlas, renderer X/Z: R shore distance (m * 4), G estimated
// depth (m * 10), B shelter, A chop; see src/boat_water_atlas.h. atlasRect holds the origin and
// 1/extent. Without one, the hand-written dock field below stands in.
uniform int atlasEnabled;
uniform sampler2D waterAtlas;
uniform vec4 atlasRect;

// Shore metres, depth metres, shelter, chop; and in W the weight of the atlas, which blends to
// the hand-written field over the outermost 1% of its extent and is 0 without one.
vec4 atlasPlace(vec2 p, out float weight)
{
    weight = 0.0;
    if (atlasEnabled == 0) return vec4(64.0, 25.0, 0.0, 1.0);
    vec2 uv = (p - atlasRect.xy) * atlasRect.zw;
    vec2 edge = min(uv, vec2(1.0) - uv);
    weight = smoothstep(0.0, 0.01, min(edge.x, edge.y));
    vec4 t = texture(waterAtlas, clamp(uv, vec2(0.0), vec2(1.0)));
    return vec4(t.r * 63.75, t.g * 25.5, t.b, t.a);
}

// Authored shelter around the narrow dock. It opens gradually across the mouth;
// water elsewhere on the river remains exposed. This field changes shading only.
float handExposure(vec2 p)
{
    float dockAxis = 12.0 - 0.09 * p.y;
    float inDock = 1.0 - smoothstep(28.0, 100.0, abs(p.x - dockAxis));
    float northOfMouth = smoothstep(-190.0, -55.0, p.y);
    return 1.0 - 0.82 * inDock * northOfMouth;
}

// 0.18 in a closed dock to 1 on open water. The atlas maps upwind fetch (chop) onto the hand
// field's range, and land closing in all round (shelter) calms it further.
float exposure(vec2 p)
{
    float weight;
    vec4 place = atlasPlace(p, weight);
    float atlas = 0.18 + 0.82 * smoothstep(0.10, 0.65, place.w)
        * (1.0 - 0.4 * smoothstep(0.3, 0.8, place.z));
    return mix(handExposure(p), atlas, weight);
}

float resolved(float frequency, float footprint)
{
    return 1.0 - smoothstep(0.65, 2.5, frequency * footprint);
}


// Same bounded packet and derivatives as boat_water.c, in reflected world X/Z.
// Height and world X/Z slope from CPU-reduced phases; production and parity probes share it.
vec3 ambientWaves(vec2 world, float footprint)
{
    vec3 result = vec3(0.0);
    for (int i = 0; i < 3; ++i) {
        vec4 w = waveParameters[i];
        float filtering = waterfrontWater != 0 ? resolved(length(w.xy), footprint) : 1.0;
        float phase = dot(w.xy, world) - w.w;
        result += vec3(w.z * sin(phase), w.xy * w.z * cos(phase) * filtering);
    }
    return result;
}

// BEGIN STORM RECIPE

// BEGIN RADIAL RECIPE
uniform vec4 radialOrigin[4]; // world centre, radius, amplitude
uniform vec4 radialMotion[4]; // speed, max range
vec4 radialWave(vec2 world) {
    vec4 result=vec4(0.0);
    for(int i=0;i<4;++i) {
        vec4 o=radialOrigin[i];
        if(o.w<=0.0) continue;
        vec2 delta=world-o.xy;
        float d=length(delta), x=(d-o.z)/2.0;
        if(abs(x)>=1.0) continue;
        float q=1.0-x*x, fade=1.0-o.z/radialMotion[i].y;
        float slope=-2.0*o.w*fade*fade*x*q;
        float h=o.w*fade*fade*q*q;
        result+=vec4(h,d>0.00001 ? slope*delta/d : vec2(0.0),
            -radialMotion[i].x*slope-2.0*o.w*fade*radialMotion[i].x/radialMotion[i].y*q*q);
    }
    return result;
}
// END RADIAL RECIPE

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
    // Largest crest at the packet centre, smaller crests on either side.
    float f = cos(k*q) * edge*edge*edge;
    float df = -k*sin(k*q) * edge*edge*edge - cos(k*q)*6.0*q/width2*edge*edge;
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


// BEGIN OBJECT RECIPE -- same signed ridge and derivatives as boat_wake_objects.c and the
// piece in waterfront_wake_stamp.fs.
uniform int objectWakeEnabled;
uniform float objectWakeLife;
uniform int objectWakePlayerCount;
uniform float objectWakeTrafficLife;
uniform sampler2D objectWakeData;
float objectCrestFoam = 0.0;
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
    float narrow=max(0.0,1.0-q*q/(0.45*0.45));
    float breakup=0.55+0.225*sin(2.3*u+3.0*age)+0.225*sin(5.1*u-4.2*age);
    objectCrestFoam+=clamp(b.z/1.8,0.0,1.0)*window*envelope*narrow*narrow*narrow*breakup;
    return b.z*vec3(envelope*window*shape,gradient);
}
// The stamped field holds the same sums as the loop below, sampled bilinearly from RGBA16F
// texels: height, world X/Z slope and crest foam. Fragments outside its guard evaluate only
// the pieces the CPU found reaching past it, listed in the record texture's third row.
uniform int stampEnabled;
uniform sampler2D stampField;
uniform vec4 stampRect; // Origin X/Z, 1/extent, half-texel guard in UV.
uniform int stampOutsideCount;
vec3 objectWakeRecord(vec2 p, int i)
{
    return objectWakePiece(p,texelFetch(objectWakeData,ivec2(i,0),0),
        texelFetch(objectWakeData,ivec2(i,1),0),
        i>=objectWakePlayerCount && objectWakeTrafficLife>0.0 ? objectWakeTrafficLife : objectWakeLife);
}
vec3 objectWake(vec2 p)
{
    vec3 result=vec3(0.0);
    if (stampEnabled != 0) {
        vec2 uv=(p-stampRect.xy)*stampRect.z;
        if (all(greaterThanEqual(uv,vec2(stampRect.w))) && all(lessThanEqual(uv,vec2(1.0-stampRect.w)))) {
            vec4 s=textureLod(stampField,uv,0.0);
            objectCrestFoam=s.w;
            return s.xyz;
        }
        for (int i=0; i<256; ++i) {
            if (i>=stampOutsideCount) break;
            result+=objectWakeRecord(p,int(texelFetch(objectWakeData,ivec2(i,2),0).x));
        }
        return result;
    }
    for (int i=0; i<256; ++i) {
        if (i>=playerWakeCount) break;
        result+=objectWakeRecord(p,i);
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

// END NATIVE WAKE FIELD

// Presentation-only crest proxy. For a sinusoid, kh/length(kh, gradient) is its
// phase independent of amplitude: increasing height cannot widen the pale band.
// The energy gate keeps gentle swells clear. Keep boat_water_mesh.c in sync.
// G3b foam history and cosmetic ripples around the boat: R foam, G ripple height. Shading only;
// the analytic vertex height stays authoritative.
uniform int historyEnabled;
uniform sampler2D historyField;
uniform vec4 historyRect; // Origin X/Z, 1/extent, edge fade in metres.
uniform float historySlopeGain;
float historyFoam = 0.0;
vec2 historySlope(vec2 world, float footprint)
{
    if (historyEnabled == 0) return vec2(0.0);
    vec2 uv = (world - historyRect.xy) * historyRect.z;
    float extent = 1.0 / historyRect.z;
    vec2 edge = min(uv, vec2(1.0) - uv) * extent;
    float fade = smoothstep(0.0, historyRect.w, min(edge.x, edge.y));
    if (fade <= 0.0) return vec2(0.0);
    float texel = 1.0 / float(textureSize(historyField, 0).x);
    vec4 here = textureLod(historyField, uv, 0.0);
    float east = textureLod(historyField, uv + vec2(texel, 0.0), 0.0).g;
    float west = textureLod(historyField, uv - vec2(texel, 0.0), 0.0).g;
    float north = textureLod(historyField, uv + vec2(0.0, texel), 0.0).g;
    float south = textureLod(historyField, uv - vec2(0.0, texel), 0.0).g;
    // Strands instead of a sheet: a fixed world pattern raises the threshold foam must pass. It
    // is stretched along the foam's drift, (-0.6, -0.8), so foam tears into meandering streaks
    // about 2 m apart and broken every few metres, not round cells.
    float along = dot(world, vec2(-0.6, -0.8)), across = dot(world, vec2(0.8, -0.6));
    float lace = 0.5 + 0.25
        * sin(across * 3.4 + 1.6 * sin(along * 0.45) + 0.8 * sin(along * 1.3 + across * 0.7))
        * (0.6 + 0.4 * sin(along * 0.9 + 2.0 * sin(across * 1.1)));
    historyFoam = fade * smoothstep(lace - 0.2, lace + 0.15, here.r);
    // Ripples finer than a pixel would shimmer; fade them with the pixel footprint.
    float resolvedRipple = 1.0 - smoothstep(0.35, 0.9, footprint);
    return vec2(east - west, north - south) / (2.0 * texel * extent)
        * historySlopeGain * fade * resolvedRipple;
}
// G4: a copy of the opaque depth drawn before the water, for the water in front of quay walls
// and other submerged geometry. Only geometry behind the surface counts, so an object in front
// of the water never shades it. Raylib's projection is conventional (not reversed) GL depth.
uniform int sceneDepthEnabled;
uniform sampler2D sceneDepth;
uniform vec4 sceneDepthParams; // Near, far, 1/width, 1/height of the target.
float linearDepth(float depth)
{
    float n = sceneDepthParams.x, f = sceneDepthParams.y;
    return 2.0 * n * f / (f + n - (depth * 2.0 - 1.0) * (f - n));
}
// Metres of water along the view axis between this fragment and the opaque scene behind it:
// a quay wall or bank below the surface. -1 when unknown or nothing opaque is behind it.
float waterBehind()
{
    if (sceneDepthEnabled == 0) return -1.0;
    float opaque = texture(sceneDepth, gl_FragCoord.xy * sceneDepthParams.zw).r;
    if (opaque >= 1.0) return -1.0;
    float behind = linearDepth(opaque) - linearDepth(gl_FragCoord.z);
    return behind < 0.0 ? -1.0 : behind;
}
// Water lapping at a submerged wall: a broken, slowly moving light band about two metres wide.
// The scene has no river bed, so open water never has anything behind it. The reflection pass
// draws no water, so only the main camera reads the copy.
vec3 shoreShade(vec3 color)
{
    float behind = waterBehind();
    if (behind < 0.0) return color;
    // 24-bit depth from a 0.15 m near plane still resolves about 4 cm at 300 m; the band widens
    // with distance so it keeps about a pixel there.
    float distance = length(cameraPosition - fragWorldPosition);
    float band = (1.0 - smoothstep(0.2, 2.0 + 0.015 * distance, behind))
        * (1.0 - smoothstep(250.0, 400.0, distance));
    float lap = 0.5 + 0.5 * sin(fragWorldPosition.x * 1.9 + fragWorldPosition.z * 1.3
        + 1.5 * sin(fragWorldPosition.z * 0.7 - fragWorldPosition.x * 0.4) + detailRipplePhase.x);
    color *= 1.0 + 0.10 * band;
    return mix(color, vec3(0.70, 0.82, 0.84) * waterLight,
        0.55 * band * smoothstep(0.25, 0.7, lap));
}
// G4 far-water life, presentation only. gustWaves and glitterWaves hold X/Z wavenumbers and a
// CPU-reduced phase; squallWater the sky squall's centre X/Z, radius and growth.
uniform int farWaterEnabled;
uniform vec4 gustWaves[4];
uniform vec4 glitterWaves[5];
uniform vec4 squallWater;

// 0..1: dark, matte gust patches drifting downwind over exposed water, and the ruffled water
// under and just ahead of a grown squall. Roughness, not shade: wind ripples too small to draw
// reflect less of the bright sky, so the water there reads darker. A squall darkens no lighting,
// so the water beneath it gets no shadow of its own.
float gustAmount(vec2 p, float exposed)
{
    if (farWaterEnabled == 0 || waterfrontWater == 0) return 0.0;
    // A gentle warp keeps the summed waves from reading as a regular pattern.
    vec2 q = p + 9.0 * vec2(sin(p.y * 0.061 + 1.3), sin(p.x * 0.053 + 0.4))
        + 3.0 * vec2(sin(p.y * 0.23 + p.x * 0.07), sin(p.x * 0.19 - p.y * 0.05));
    float sum = 0.0;
    for (int i = 0; i < 4; ++i)
        sum += sin(dot(gustWaves[i].xy, q) - gustWaves[i].z);
    float gust = smoothstep(1.3, 2.5, sum) * smoothstep(0.35, 0.8, exposed);
    if (squallWater.w > 0.0 && squallWater.z > 0.0) {
        float d = length(p - squallWater.xy) / squallWater.z;
        gust = max(gust, 0.5 * squallWater.w * (1.0 - smoothstep(0.8, 1.7, d)));
    }
    return gust;
}

// Chunky sun glitter: short oblique waves tilt the surface, and where one throws the sun into
// the eye inside the broad path toward it, the pixel lights fully. Farther pixels average more
// facets, so the threshold widens with the footprint and the path stays sparkling.
float sunGlitter(vec3 normal, vec3 viewDirection, float footprint, float exposed)
{
    if (farWaterEnabled == 0 || waterfrontWater == 0) return 0.0;
    vec3 sun = normalize(sunDirection);
    float path = smoothstep(0.90, 0.985, dot(reflect(-viewDirection, normal), sun));
    if (path <= 0.0) return 0.0;
    vec2 p = fragWorldPosition.xz;
    p += 0.9 * vec2(sin(p.y * 0.83 + 2.0 * sin(p.x * 0.31)), sin(p.x * 0.71 + 2.0 * sin(p.y * 0.37)));
    vec2 tilt = vec2(0.0);
    for (int i = 0; i < 5; ++i) {
        vec2 k = glitterWaves[i].xy;
        tilt += k / length(k) * cos(dot(k, p) - glitterWaves[i].z);
    }
    vec3 facet = normalize(vec3(normal.x - 0.11 * tilt.x, normal.y, normal.z - 0.11 * tilt.y));
    float align = dot(reflect(-viewDirection, facet), sun);
    float threshold = 1.0 - 0.0012 * (1.0 + 3.0 * min(footprint, 4.0));
    // Close up a glint band spans many pixels and reads as a worm, not a point; a grown squall
    // hides the sun everywhere.
    float distance = length(cameraPosition - fragWorldPosition);
    float sparkle = path * step(threshold, align) * mix(0.35, 1.0, exposed)
        * smoothstep(10.0, 35.0, distance) * (1.0 - 0.9 * squallWater.w);
    if (glintSparkle >= 1.0) return sparkle;
    // A calm glitter keeps fewer sparkles, and only where a pixel resolves single facets:
    // farther away they would average into the path, and alone they read as static.
    float keep = step(fract(dot(floor(p * 1.7), vec2(0.6180, 0.3819)) * 7.31), glintSparkle);
    return sparkle * keep * (1.0 - smoothstep(1.5, 4.5, footprint));
}
// The calm glitter's road of light: the sun mirrored in a gently rippled surface, narrow across
// and long toward the horizon, in a few hard steps of brightness rather than a smooth glow or a
// per-pixel threshold, so it reads as pixels and shimmers slowly instead of flickering.
float sunSheen(vec3 normal, vec3 viewDirection, float exposed)
{
    if (glintSheen <= 0.0 || farWaterEnabled == 0 || waterfrontWater == 0) return 0.0;
    vec3 sun = normalize(sunDirection);
    vec3 calm = normalize(mix(normal, vec3(0.0, 1.0, 0.0), 0.35));
    vec3 r = reflect(-viewDirection, calm);
    vec2 across = normalize(r.xz), toward = normalize(sun.xz);
    float bearing = abs(atan(across.x * toward.y - across.y * toward.x, dot(across, toward)));
    float rise = abs(asin(clamp(r.y, -1.0, 1.0)) - asin(sun.y));
    float road = (1.0 - smoothstep(0.035, 0.09, bearing)) * (1.0 - smoothstep(0.06, 0.40, rise));
    float distance = length(cameraPosition - fragWorldPosition);
    // Broken into slow dashes across the road, drifting toward the viewer like swell.
    vec2 w = fragWorldPosition.xz, side = vec2(-toward.y, toward.x);
    float swell = sin(dot(w, toward) * 1.3 + 1.6 * sin(dot(w, side) * 0.45) + detailRipplePhase.x);
    road *= mix(0.4, 1.0, step(-0.25, swell));
    road *= mix(0.6, 1.0, exposed) * smoothstep(10.0, 35.0, distance) * (1.0 - 0.9 * squallWater.w);
    return glintSheen * floor(road * 3.0 + 0.35) / 3.0;
}
// G5b: the city's clustered lights (waterfront_lights.h) and warm reflections at dusk. Zero window
// light, as by day, leaves the water as it was.
uniform float windowLight;
uniform vec2 reflectionTexel; // 1 / reflection target size.
uniform int lightClusterMode; // 0 none (the capped fallback lights only the city), 1 slots, 2 all.
uniform highp sampler2D lightClusters; // Float records: keep full precision on the web.
uniform int lightClusterCount;
uniform vec4 lightClusterLayout; // Cell metres, grid slots per axis, slot base, index base.

vec4 lightTexel(int index)
{
    return texelFetch(lightClusters, ivec2(index % 256, index / 256), 0);
}

// The lights listed for this point: the slot's (offset, count), or every light in mode 2.
ivec2 lightList()
{
    if (lightClusterMode == 2) return ivec2(0, lightClusterCount);
    vec2 cell = mod(floor(fragWorldPosition.xz / lightClusterLayout.x), lightClusterLayout.y);
    int slot = int(cell.y) * int(lightClusterLayout.y) + int(cell.x);
    vec4 header = lightTexel(int(lightClusterLayout.z) + slot);
    return ivec2(int(header.x), int(header.y));
}

int listedLight(ivec2 list, int k)
{
    if (lightClusterMode == 2) return k;
    int at = list.x + k;
    vec4 packed = lightTexel(int(lightClusterLayout.w) + at / 4);
    return int(packed[at % 4]);
}

// Lamplight on the water: a faint pool under each light and a glint where the rippled surface
// throws it toward the eye, from the same lights that shade the quays.
vec3 waterLocalLight(vec3 normal, vec3 viewDirection)
{
    if (lightClusterMode == 0 || windowLight <= 0.0) return vec3(0.0);
    vec3 sum = vec3(0.0);
    ivec2 list = lightList();
    vec3 mirrored = reflect(-viewDirection, normal);
    for (int k = 0; k < list.y; ++k) {
        int base = listedLight(list, k) * 4;
        vec4 positionRadius = lightTexel(base);
        vec3 delta = positionRadius.xyz - fragWorldPosition;
        float distance = length(delta);
        if (distance >= positionRadius.w) continue;
        vec3 towardLight = delta / max(distance, 0.001);
        vec4 colorIntensity = lightTexel(base + 1);
        vec4 cone = lightTexel(base + 3);
        float spot = 1.0;
        if (cone.y >= 0.0)
            spot = smoothstep(cone.y, cone.x, dot(normalize(lightTexel(base + 2).xyz), -towardLight));
        float attenuation = 1.0 - distance / positionRadius.w;
        float glint = pow(max(dot(mirrored, towardLight), 0.0), 48.0);
        sum += colorIntensity.rgb * (colorIntensity.a * attenuation * attenuation * spot
            * (0.16 * max(towardLight.y, 0.0) + 1.2 * glint));
    }
    return sum * windowLight;
}

// Warm emitters in the dusk reflection: lit windows, lamps and lenses. The dusk sky and scenery
// are blue, and the afterglow is too dim in green to pass.
float emitterKey(vec3 c)
{
    return smoothstep(0.20, 0.36, c.r - c.b) * smoothstep(0.52, 0.70, c.g);
}

// Long stepped streaks: each emitter seen in rippled water smears along the reflection's
// vertical axis, longest toward the viewer, broken into dashes by the ripples and held to three
// steps of brightness so it reads as pixels.
vec4 duskStreak(vec2 uv, float exposed)
{
    // Only after sunset: a warm sunset sky passes the key too and would streak its clouds.
    float dusk = smoothstep(0.5, 1.0, windowLight);
    if (dusk <= 0.0) return vec4(0.0);
    vec3 color = vec3(0.0);
    float reach = 0.0;
    for (int i = -4; i <= 10; ++i) {
        float v = uv.y + float(i) * 2.0 * reflectionTexel.y;
        if (v < 0.0 || v > 1.0) continue;
        vec3 c = texture(reflectionTexture, vec2(uv.x, v)).rgb;
        float weight = emitterKey(c) * (1.0 - abs(float(i)) / (i < 0 ? 6.0 : 12.0));
        if (weight > reach) {
            reach = weight;
            color = c;
        }
    }
    vec2 w = fragWorldPosition.xz;
    float swell = sin(w.y * 1.9 + 1.4 * sin(w.x * 0.55) + detailRipplePhase.x)
        + 0.6 * sin(w.x * 2.7 - w.y * 0.8 + detailRipplePhase.y);
    reach *= mix(0.25, 1.0, step(-0.35, swell)) * mix(1.0, 0.75, exposed);
    return vec4(color, floor(reach * 3.0 + 0.3) / 3.0 * dusk);
}
float waveCrestFoam(vec3 wave, float k)
{
    float kh = k * wave.x;
    float energy = length(vec3(kh, wave.yz));
    return smoothstep(0.90, 0.99, kh / max(energy, 0.0001))
        * smoothstep(0.12, 0.50, energy);
}
void main()
{
    float footprint = max(length(dFdx(fragWorldPosition.xz)),
        length(dFdy(fragWorldPosition.xz))) * surfacePixelScale;
    float boatDistance = length(fragWorldPosition.xz - wavePatch.xy);
    if (waterPatchPass == 1 && (wakeFieldEnabled != 0 || boatDistance < wavePatch.w)) discard;
    if (waterPatchPass == 2 && wakeFieldEnabled == 0 && boatDistance >= wavePatch.w) discard;
    vec3 base = texture(texture0, fragTexCoord).rgb * objectColor.rgb * fragColor.rgb * waterLight;
    vec3 normal = normalize(fragWorldNormal);
    float exposed = exposure(fragWorldPosition.xz);
    float gust = gustAmount(fragWorldPosition.xz, exposed);
    vec3 wake = vec3(0.0);
    vec3 vessel = vesselWake(fragWorldPosition.xz).xyz;
    vec3 storm = stormWave(fragWorldPosition.xz).xyz;
    vec3 radial = radialWave(fragWorldPosition.xz).xyz;
    if (physicalWaves != 0) {
        vec3 ambient = ambientWaves(fragWorldPosition.xz, footprint);
        vec2 slope = ambient.yz;
        float surfaceHeight = ambient.x;
        wake = crossingWake(fragWorldPosition.xz, crossingWakeTime)
            + playerWake(fragWorldPosition.xz)
            + wakeFieldSample(fragWorldPosition.xz) + vessel + storm + radial;
        slope += wake.yz;
        surfaceHeight += wake.x;
        if (waterPatchPass != 0 && wakeFieldEnabled == 0) {
            float span = wavePatch.w - wavePatch.z;
            float t = clamp((boatDistance - wavePatch.z) / span, 0.0, 1.0);
            float fade = 1.0 - t*t*(3.0-2.0*t);
            vec2 fadeGradient = -6.0*t*(1.0-t)/span
                * (fragWorldPosition.xz-wavePatch.xy)/max(boatDistance,0.001);
            slope = slope*fade + surfaceHeight*fadeGradient;
        }
        if (waterfrontWater != 0 && objectWakeEnabled == 0) slope *= mix(0.55, 1.0, exposed);
        normal = normalize(vec3(-slope.x, 1.0, -slope.y));
        base *= vec3(0.78, 0.94, 1.0) * (0.95 + 0.35 * dot(normal.xz, vec2(0.7)));
    } else {
        base *= vec3(0.72, 0.90, 1.0);
    }
    if (waterfrontWater != 0) {
        vec2 wind = vec2(-0.6, -0.8), crossing = vec2(0.8, -0.6);
        float longRipple = dot(wind, fragWorldPosition.xz) * 1.65 - detailRipplePhase.x;
        float shortRipple = dot(crossing, fragWorldPosition.xz) * 3.7 - detailRipplePhase.y;
        vec2 detail = wind * cos(longRipple) * resolved(1.65, footprint)
            + crossing * cos(shortRipple) * 0.45 * resolved(3.7, footprint);
        vec2 slope = normal.xz + detail * mix(0.004, 0.035, exposed) * (1.0 + 1.2 * gust);
        normal = normalize(vec3(slope.x, normal.y, slope.y));
        base *= mix(vec3(0.97, 1.01, 1.0), vec3(1.04, 1.07, 0.98), exposed);
        // Estimated shallows warm and lighten a little; the last metres before a shore lighten.
        float weight;
        vec4 place = atlasPlace(fragWorldPosition.xz, weight);
        float shallow = (1.0 - smoothstep(0.8, 2.0, place.y)) * weight;
        float shore = (1.0 - smoothstep(0.5, 4.0, place.x)) * weight;
        base = mix(base, base * vec3(1.06, 1.10, 1.0) + vec3(0.02, 0.03, 0.01), 0.35 * shallow);
        base *= 1.0 + 0.06 * shore;
        base *= 1.0 - 0.14 * gust;
    }
    // Ripples read clearest on sheltered, glassy water and break up in chop.
    vec2 ripples = historySlope(fragWorldPosition.xz, footprint)
        * (waterfrontWater != 0 ? mix(1.25, 0.85, exposed) : 1.0);
    if (historyEnabled != 0) {
        normal = normalize(vec3(normal.x - ripples.x * normal.y, normal.y,
            normal.z - ripples.y * normal.y));
        base *= 1.0 + 0.35 * dot(ripples, vec2(0.7));
    }
    vec3 viewDirection = normalize(cameraPosition - fragWorldPosition);
    vec3 lamplight = waterLocalLight(normal, viewDirection);
    if (reflectionsEnabled == 0) {
        base = shoreShade(mix(base, vec3(0.65, 0.78, 0.80) * waterLight, 0.55 * historyFoam));
        finalColor = vec4(base * waterShadow() + lamplight, 1.0);
        return;
    }

    vec4 reflectedPosition = reflectionViewProjection * vec4(fragWorldPosition, 1.0);
    vec2 reflectionUv = reflectedPosition.xy / max(reflectedPosition.w, 0.0001) * 0.5 + 0.5;
    vec2 ripple = vec2(sin(fragWorldPosition.x * 0.09 + reflectionRipplePhase.x)
            + sin(fragWorldPosition.z * 0.05 - reflectionRipplePhase.y),
        sin(fragWorldPosition.z * 0.08 + reflectionRipplePhase.z)
            + sin(fragWorldPosition.x * 0.04 + reflectionRipplePhase.w)) * 0.0025;
    if (physicalWaves != 0) ripple = normal.xz * 0.08;
    if (waterfrontWater != 0 && physicalWaves == 0)
        ripple = normal.xz * 0.06 + ripple * mix(0.25, 0.75, exposed);
    reflectionUv += ripple;
    vec2 edge = min(reflectionUv, vec2(1.0) - reflectionUv);
    float coverage = smoothstep(0.0, 0.025, min(edge.x, edge.y));
    if (reflectedPosition.w <= 0.0) coverage = 0.0;
    reflectionUv = clamp(reflectionUv, vec2(0.004), vec2(0.996));
    vec3 reflection = texture(reflectionTexture, reflectionUv).rgb;
    float viewDot = max(0.0, dot(normal, viewDirection));
    float reflectionAmount = mix(0.10, physicalWaves != 0 ? 0.30 : 0.42,
        (1.0 - viewDot) * (1.0 - viewDot));
    if (waterfrontWater != 0)
        reflectionAmount = mix(0.13, mix(0.38, 0.25, exposed),
            (1.0 - viewDot) * (1.0 - viewDot));
    reflectionAmount *= 1.0 - 0.5 * gust;
    vec3 color = mix(base, reflection * vec3(0.96, 0.99, 1.0), reflectionAmount * coverage);
    if (physicalWaves != 0) {
        vec3 halfVector = normalize(viewDirection + normalize(sunDirection));
        vec3 highlight = waterfrontWater != 0 ? vec3(0.09, 0.10, 0.085) * mix(0.25, 1.0, exposed)
            : vec3(0.28, 0.25, 0.17);
        color += highlight * highlightTint * pow(max(dot(normal, halfVector), 0.0), 96.0);
    }
    if (wakeAmplitude > 0.0 || playerWakeCount > 0 || wakeFieldEnabled != 0 || vesselMotion.y > 0.0 || stormArea.w > 0.0 || radial.x > 0.0) {
        float wakeHeight = wake.x - vessel.x - storm.x;
        float crest = objectWakeEnabled != 0
            ? clamp(objectCrestFoam, 0.0, 1.0) * clamp(wakeHeight / 0.3, 0.0, 1.0)
            : clamp(wakeHeight / 0.38, 0.0, 1.0);
        float foam = crest * 0.65;
        foam = max(foam, 0.38 * waveCrestFoam(vessel, 6.2831853 / max(vesselShape.y, 0.001)));
        foam = max(foam, 0.28 * waveCrestFoam(storm, 0.42));
        foam = max(foam, 0.8 * waveCrestFoam(radial, 0.4));
        // The compact rain footprint darkens only the rough patch.
        vec2 stormDelta = (fragWorldPosition.xz-stormArea.xy)/max(stormArea.z,1.0);
        float stormShade = pow(max(0.0,1.0-dot(stormDelta,stormDelta)),2.0);
        if (stormArea.w > 0.0) color *= 1.0-0.32*stormShade;
        color = mix(color, vec3(0.65, 0.78, 0.80) * waterLight, foam);
    }
    color = mix(color, vec3(0.65, 0.78, 0.80) * waterLight, 0.55 * historyFoam);
    color = shoreShade(color);
    if (any(greaterThan(glintColor, vec3(0.0)))) {
        float glint = 0.75 * sunGlitter(normal, viewDirection, footprint, exposed);
        if (glintSheen > 0.0) glint = max(glint, sunSheen(normal, viewDirection, exposed));
        color = mix(color, glintColor, glint);
    }
    vec4 streak = duskStreak(reflectionUv, exposed);
    color = mix(color, streak.rgb, streak.a * coverage);
    finalColor = vec4(color * waterShadow() + lamplight, 1.0);
}

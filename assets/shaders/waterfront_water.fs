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
uniform int physicalWaves;
uniform float wakeAmplitude;
uniform vec2 wakeOffset; // World X/Z translation of the scripted source.
uniform int playerWakeCount;
uniform vec4 playerWakeOrigins[64];
uniform vec4 playerWakeParameters[64];
uniform vec4 waveParameters[3]; // World X/Z wave vector, amplitude, CPU-reduced phase.
uniform int reflectionsEnabled;
uniform int shadowsEnabled;
uniform sampler2D shadowMap;
uniform mat4 lightViewProjection;
uniform int waterPatchPass;
uniform vec4 wavePatch;
uniform int waterfrontWater; // Only playable Eriksberg; leave timeline/basin materials alone.
uniform float surfacePixelScale;

out vec4 finalColor;

float waterShadow()
{
    if (shadowsEnabled == 0) return 1.0;
    vec4 lightPosition = lightViewProjection * vec4(fragWorldPosition, 1.0);
    vec3 p = lightPosition.xyz / lightPosition.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    return p.z - 0.00018 > texture(shadowMap, p.xy).r ? 0.55 : 1.0;
}

// Authored shelter around the narrow dock. It opens gradually across the mouth;
// water elsewhere on the river remains exposed. This field changes shading only.
float exposure(vec2 p)
{
    float dockAxis = 12.0 - 0.09 * p.y;
    float inDock = 1.0 - smoothstep(28.0, 100.0, abs(p.x - dockAxis));
    float northOfMouth = smoothstep(-190.0, -55.0, p.y);
    return 1.0 - 0.82 * inDock * northOfMouth;
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

// END NATIVE WAKE FIELD
void main()
{
    float footprint = max(length(dFdx(fragWorldPosition.xz)),
        length(dFdy(fragWorldPosition.xz))) * surfacePixelScale;
    float boatDistance = length(fragWorldPosition.xz - wavePatch.xy);
    if (waterPatchPass == 1 && (wakeFieldEnabled != 0 || boatDistance < wavePatch.w)) discard;
    if (waterPatchPass == 2 && wakeFieldEnabled == 0 && boatDistance >= wavePatch.w) discard;
    vec3 base = texture(texture0, fragTexCoord).rgb * objectColor.rgb * fragColor.rgb;
    vec3 normal = normalize(fragWorldNormal);
    float exposed = exposure(fragWorldPosition.xz);
    vec3 wake = vec3(0.0);
    if (physicalWaves != 0) {
        vec3 ambient = ambientWaves(fragWorldPosition.xz, footprint);
        vec2 slope = ambient.yz;
        float surfaceHeight = ambient.x;
        wake = crossingWake(fragWorldPosition.xz, crossingWakeTime)
            + playerWake(fragWorldPosition.xz)
            + wakeFieldSample(fragWorldPosition.xz) + vesselWake(fragWorldPosition.xz).xyz + stormWave(fragWorldPosition.xz).xyz;
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
        vec2 slope = normal.xz + detail * mix(0.004, 0.035, exposed);
        normal = normalize(vec3(slope.x, normal.y, slope.y));
        base *= mix(vec3(0.97, 1.01, 1.0), vec3(1.04, 1.07, 0.98), exposed);
    }
    if (reflectionsEnabled == 0) {
        finalColor = vec4(base * waterShadow(), 1.0);
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
    vec3 viewDirection = normalize(cameraPosition - fragWorldPosition);
    float viewDot = max(0.0, dot(normal, viewDirection));
    float reflectionAmount = mix(0.10, physicalWaves != 0 ? 0.30 : 0.42,
        (1.0 - viewDot) * (1.0 - viewDot));
    if (waterfrontWater != 0)
        reflectionAmount = mix(0.13, mix(0.38, 0.25, exposed),
            (1.0 - viewDot) * (1.0 - viewDot));
    vec3 color = mix(base, reflection * vec3(0.96, 0.99, 1.0), reflectionAmount * coverage);
    if (physicalWaves != 0) {
        vec3 halfVector = normalize(viewDirection + normalize(sunDirection));
        vec3 highlight = waterfrontWater != 0 ? vec3(0.09, 0.10, 0.085) * mix(0.25, 1.0, exposed)
            : vec3(0.28, 0.25, 0.17);
        color += highlight * pow(max(dot(normal, halfVector), 0.0), 96.0);
    }
    if (wakeAmplitude > 0.0 || playerWakeCount > 0 || wakeFieldEnabled != 0 || vesselMotion.y > 0.0 || stormArea.w > 0.0) {
        float crest = objectWakeEnabled != 0
            ? clamp(objectCrestFoam, 0.0, 1.0) * clamp(wake.x / 0.3, 0.0, 1.0)
            : clamp(wake.x / 0.38, 0.0, 1.0);
        crest = max(crest, clamp(vesselWake(fragWorldPosition.xz).x / 0.8, 0.0, 1.0));
        crest = max(crest, clamp(stormWave(fragWorldPosition.xz).x / 0.8, 0.0, 1.0));
        // The compact rain footprint darkens only the rough patch.
        vec2 stormDelta = (fragWorldPosition.xz-stormArea.xy)/max(stormArea.z,1.0);
        float stormShade = pow(max(0.0,1.0-dot(stormDelta,stormDelta)),2.0);
        if (stormArea.w > 0.0) color *= 1.0-0.32*stormShade;
        color = mix(color, vec3(0.65, 0.78, 0.80), crest * 0.65);
    }
    finalColor = vec4(color * waterShadow(), 1.0);
}

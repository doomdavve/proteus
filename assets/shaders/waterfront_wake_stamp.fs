#version 330

in vec2 stampWorld;
flat in int stampPiece;

// 0: one object wake piece, summed by additive blending into the RGBA16F field as height,
// world X/Z slope and crest foam. 1: write probeValue. 2: compare probeField's first texel with
// probeValue, white when it matches.
uniform int stampMode;
uniform vec4 probeValue;
uniform sampler2D probeField;
uniform int objectWakePlayerCount;
uniform float objectWakeLife;
uniform float objectWakeTrafficLife;
uniform sampler2D objectWakeData;

out vec4 finalColor;

// BEGIN OBJECT PIECE -- same as objectWakePiece in waterfront_water.fs and boat_wake_objects.c.
vec4 objectWakePiece(vec2 p, vec4 a, vec4 b, float life)
{
    vec2 delta=p-a.xy, direction=a.zw;
    float u=dot(delta,direction);
    if (abs(u)>=b.x) return vec4(0.0);
    vec2 normal=vec2(-direction.y,direction.x);
    float v=dot(delta,normal), radius=sqrt(v*v+0.25);
    float clock=b.y, age=clock-u*b.w;
    float q=radius-0.9-2.4*age;
    if (q<=-7.0 || q>=2.0) return vec4(0.0);
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
    float foam=clamp(b.z/1.8,0.0,1.0)*window*envelope*narrow*narrow*narrow*breakup;
    return vec4(b.z*vec3(envelope*window*shape,gradient),foam);
}
// END OBJECT PIECE

void main()
{
    if (stampMode == 1) {
        finalColor = probeValue;
        return;
    }
    if (stampMode == 2) {
        vec4 error = abs(texelFetch(probeField, ivec2(0), 0) - probeValue);
        finalColor = max(max(error.x, error.y), max(error.z, error.w)) < 0.001
            ? vec4(1.0) : vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float life = stampPiece >= objectWakePlayerCount && objectWakeTrafficLife > 0.0
        ? objectWakeTrafficLife : objectWakeLife;
    finalColor = objectWakePiece(stampWorld, texelFetch(objectWakeData, ivec2(stampPiece, 0), 0),
        texelFetch(objectWakeData, ivec2(stampPiece, 1), 0), life);
}

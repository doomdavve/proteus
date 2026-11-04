#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
// w is 1 + the baked ambient occlusion (0..255) of map meshes; three-float normals read 1.
in vec4 vertexNormal;
in vec4 vertexColor;

// Locations are fixed because one mesh VAO serves this program and the shadow program.
layout(location = 4) in mat4 instanceMvp;
layout(location = 8) in vec4 instanceModelRow0;
layout(location = 9) in vec4 instanceModelRow1;
layout(location = 10) in vec4 instanceModelRow2;
layout(location = 11) in mat3 instanceNormal;
layout(location = 14) in vec4 instanceColor;
layout(location = 15) in float instanceEmissive;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform vec4 objectColor;
uniform int emissive;
uniform int instanced;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragWorldPosition;
out vec3 fragWorldNormal;
// Flat: interpolating a constant across a triangle does not reproduce it exactly.
flat out vec4 fragObjectColor;
flat out int fragEmissive;
out float fragOcclusion;

void main()
{
    // Only the inputs differ between scalar and instanced draws. One arithmetic path keeps the
    // compiled rounding identical, which their byte-parity check depends on.
    mat4 objectMvp = mvp;
    mat4 objectModel = matModel;
    mat4 objectNormal = matNormal;
    fragObjectColor = objectColor;
    fragEmissive = emissive;
    if (instanced != 0) {
        objectMvp = instanceMvp;
        objectModel = transpose(mat4(instanceModelRow0, instanceModelRow1, instanceModelRow2,
            vec4(0.0, 0.0, 0.0, 1.0)));
        objectNormal = mat4(instanceNormal);
        fragObjectColor = instanceColor;
        fragEmissive = int(instanceEmissive);
    }
    vec4 worldPosition = objectModel * vec4(vertexPosition, 1.0);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragWorldPosition = worldPosition.xyz;
    fragWorldNormal = normalize((objectNormal * vec4(vertexNormal.xyz, 0.0)).xyz);
    fragOcclusion = clamp((vertexNormal.w - 1.0) / 255.0, 0.0, 1.0);
    gl_Position = objectMvp * vec4(vertexPosition, 1.0);
}

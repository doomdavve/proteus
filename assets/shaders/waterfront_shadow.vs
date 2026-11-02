#version 330

in vec3 vertexPosition;
layout(location = 4) in mat4 instanceMvp;

uniform mat4 mvp;
uniform int instanced;

void main()
{
    mat4 objectMvp = mvp;
    if (instanced != 0)
        objectMvp = instanceMvp;
    gl_Position = objectMvp * vec4(vertexPosition, 1.0);
}

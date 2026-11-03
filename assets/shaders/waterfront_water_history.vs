#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;

uniform mat4 mvp;

out vec2 historyUv;

void main()
{
    historyUv = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}

#version 330

in vec3 vertexPosition; // Field texels.
in vec2 vertexTexCoord; // Shader world X/Z.
in vec4 vertexColor; // Piece index: red + 256 * green.

uniform mat4 mvp;

out vec2 stampWorld;
flat out int stampPiece;

void main()
{
    stampWorld = vertexTexCoord;
    stampPiece = int(vertexColor.r * 255.0 + 0.5) + 256 * int(vertexColor.g * 255.0 + 0.5);
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}

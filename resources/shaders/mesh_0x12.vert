#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec3 aColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform mat4 mvp;
uniform mat4 modelView;
uniform mat3 normalMatrix;

out vec2 vUV;
out vec4 vNormal;
out vec4 oViewPosition;
out vec4 oWorldPosition; // for shadow-space sampling in the fragment shader
out vec3 oVertexColor;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0f);
    vUV = aUV;

    //mat3 normalMat = mat3(transpose(inverse(view * model)));
    //vec3 normalVS = normalize(normalMat * aNormal);
    //vNormal = vec4(normalVS, 1.0f);

    vec3 normalVS = normalize(normalMatrix * aNormal);
    vNormal = vec4(normalVS, 1.0f);

    oViewPosition  = modelView * vec4(aPos, 1.0f);
    oWorldPosition = model * vec4(aPos, 1.0f);

    oVertexColor = aColor;
}

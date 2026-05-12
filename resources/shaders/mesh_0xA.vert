#version 330 core
layout (location = 0) in vec4 aPos;
layout (location = 1) in vec4 aNormal;
layout (location = 2) in vec2 aUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform mat4 viewProj;
uniform mat3 viewNormalMatrix;

out vec2 vUV;
out vec4 vNormal;
out vec4 oViewPosition;
out vec4 oWorldPosition;
out vec3 oVertexColor;

void main()
{
    vUV = aUV;

    gl_Position = projection * view * aPos;

    //mat3 normalMat = mat3(transpose(inverse(view)));
    //vec3 normalVS = normalize(normalMat * vec3(aNormal));
    //vNormal = vec4(normalVS, 1.0f);

    vec3 normalVS = normalize(viewNormalMatrix * vec3(aNormal));
    vNormal = vec4(normalVS, 1.0f);

    vec4 a = view * aPos;
    oViewPosition = a;
    oWorldPosition = aPos;
    oVertexColor = vec3(0.0f);

    /*
    vUV = aUV;

    gl_Position = viewProj * aPos;

    vec3 normalVS = normalize(viewNormalMatrix * vec3(aNormal));
    vNormal = vec4(normalVS, 1.0f);

    oViewPosition = view * aPos;
    // 0xA is already in world space (CPU-skinned); position passes through
    // unchanged for the shadow projection.
    oWorldPosition = aPos;
    // 0xA shares the no-colour pipeline with 0x2. Same VU dispatch (sgsuv
    // DRAWTYPE2 → CalcSpotPoint), no SgPreRender pre-bake. Use a zero base so
    // the frag's full dynamic lighting (uMeshLightingMode=0) shows through;
    // see mesh_0x2.vert for the full rationale.
    oVertexColor = vec3(0.0f);
    */
}

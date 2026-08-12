#version 450

layout(set = 0, binding = 0) uniform PerFrame {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrix;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
    vec4 viewPos;
    vec4 fogColor;
    vec4 fogParams;
    vec4 shadowParams;
};

// Per-draw push constants (batch-level data only)
layout(push_constant) uniform Push {
    int texCoordSet;         // UV set index (0 or 1)
    int isFoliage;           // Foliage wind animation flag
    int instanceDataOffset;  // Base index into InstanceSSBO for this draw group
} push;

layout(set = 2, binding = 0) readonly buffer BoneSSBO {
    mat4 bones[];
};

// Per-instance data read via gl_InstanceIndex (GPU instancing)
struct InstanceData {
    mat4 model;
    vec2 uvOffset;
    float fadeAlpha;
    int useBones;
    int boneBase;
    int boneCount;
};
layout(set = 3, binding = 0) readonly buffer InstanceSSBO {
    InstanceData instanceData[];
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aBoneWeights;
layout(location = 4) in vec4 aBoneIndicesF;
layout(location = 5) in vec2 aTexCoord2;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;
layout(location = 3) flat out vec3 InstanceOrigin;
layout(location = 4) out float ModelHeight;
layout(location = 5) out float vFadeAlpha;
layout(location = 6) flat out int vSkyMode;

void main() {
    // Fetch per-instance data from SSBO
    int instIdx = push.instanceDataOffset + gl_InstanceIndex;
    mat4 model = instanceData[instIdx].model;
    vec2 uvOff = instanceData[instIdx].uvOffset;
    float fade = instanceData[instIdx].fadeAlpha;
    int uBones = instanceData[instIdx].useBones;
    int bBase  = instanceData[instIdx].boneBase;

    vec4 pos = vec4(aPos, 1.0);
    vec4 norm = vec4(aNormal, 0.0);

    if (uBones != 0) {
        // Clamp to the range this instance actually owns. A model whose bone
        // count exceeds the ceiling would otherwise index into the next
        // instance's matrices and fling its vertices across the world.
        int bCount = max(instanceData[instIdx].boneCount, 1);
        ivec4 bi = clamp(ivec4(aBoneIndicesF), ivec4(0), ivec4(bCount - 1));
        mat4 skinMat = bones[bBase + bi.x] * aBoneWeights.x
                     + bones[bBase + bi.y] * aBoneWeights.y
                     + bones[bBase + bi.z] * aBoneWeights.z
                     + bones[bBase + bi.w] * aBoneWeights.w;
        pos = skinMat * pos;
        norm = skinMat * norm;
    }

    // Wind animation for foliage
    if (push.isFoliage > 0) {
        float windTime = fogParams.z;
        vec3 worldRef = model[3].xyz;
        float heightFactor = clamp(pos.z / 20.0, 0.0, 1.0);
        heightFactor *= heightFactor; // quadratic - base stays grounded

        // Layer 1: Trunk sway - slow, large amplitude
        float trunkPhase = windTime * 0.8 + dot(worldRef.xy, vec2(0.1, 0.13));
        float trunkSwayX = sin(trunkPhase) * 0.35 * heightFactor;
        float trunkSwayY = cos(trunkPhase * 0.7) * 0.25 * heightFactor;

        // Layer 2: Branch sway - medium frequency, per-branch phase
        float branchPhase = windTime * 1.7 + dot(worldRef.xy, vec2(0.37, 0.71));
        float branchSwayX = sin(branchPhase + pos.y * 0.4) * 0.15 * heightFactor;
        float branchSwayY = cos(branchPhase * 1.1 + pos.x * 0.3) * 0.12 * heightFactor;

        // Layer 3: Leaf flutter - fast, small amplitude, per-vertex
        float leafPhase = windTime * 4.5 + dot(aPos, vec3(1.7, 2.3, 0.9));
        float leafFlutterX = sin(leafPhase) * 0.06 * heightFactor;
        float leafFlutterY = cos(leafPhase * 1.3) * 0.05 * heightFactor;

        pos.x += trunkSwayX + branchSwayX + leafFlutterX;
        pos.y += trunkSwayY + branchSwayY + leafFlutterY;
    }

    vec4 worldPos = model * pos;
    FragPos = worldPos.xyz;
    Normal = mat3(model) * norm.xyz;

    TexCoord = (push.texCoordSet == 1 ? aTexCoord2 : aTexCoord) + uvOff;

    InstanceOrigin = model[3].xyz;
    ModelHeight = pos.z;
    vFadeAlpha = fade;
    vSkyMode = push.isFoliage < 0 ? 1 : 0;

    gl_Position = projection * view * worldPos;
}

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
    vec4 localLightPosRadius[64];
    vec4 localLightColorIntensity[64];
    ivec4 localLightMeta;
};

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(set = 1, binding = 2) uniform M2Material {
    int hasTexture;
    int alphaTest;
    int colorKeyBlack;
    float colorKeyThreshold;
    int unlit;
    int blendMode;
    float fadeAlpha;
    float interiorDarken;
    float specularIntensity;
    float emissiveBoost;
};

layout(set = 0, binding = 1) uniform sampler2DShadow uShadowMap;

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) flat in vec3 InstanceOrigin;
layout(location = 4) in float ModelHeight;
layout(location = 5) in float vFadeAlpha;
layout(location = 6) flat in int vSkyMode;

layout(location = 0) out vec4 outColor;

const float SHADOW_TEXEL = 1.0 / 4096.0;

float sampleShadowPCF(sampler2DShadow smap, vec3 coords) {
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            shadow += texture(smap, vec3(coords.xy + vec2(x, y) * SHADOW_TEXEL, coords.z));
        }
    }
    return shadow / 9.0;
}

vec3 localLightContribution(vec3 pos, vec3 normal, vec3 albedo) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < min(localLightMeta.x, 64); ++i) {
        vec3 toLight = localLightPosRadius[i].xyz - pos;
        float dist = length(toLight);
        float radius = localLightPosRadius[i].w;
        if (dist >= radius || radius <= 0.0) continue;
        float attenuation = 1.0 - dist / radius;
        attenuation *= attenuation;
        float wrappedDiffuse = 0.22 + 0.78 * max(dot(normal, toLight / max(dist, 0.001)), 0.0);
        sum += albedo * localLightColorIntensity[i].rgb *
               (localLightColorIntensity[i].w * attenuation * wrappedDiffuse);
    }
    return sum;
}

// 4x4 Bayer dither matrix (normalized to 0..1)
float bayerDither4x4(ivec2 p) {
    int idx = (p.x & 3) + (p.y & 3) * 4;
    float m[16] = float[16](
         0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
        12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
         3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
        15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
    );
    return m[idx];
}

void main() {
    vec4 texColor = hasTexture != 0 ? texture(uTexture, TexCoord) : vec4(1.0);

    bool isFoliage = (alphaTest == 2);

    // Fix DXT fringe: transparent edge texels have garbage (black) RGB.
    // At low alpha the original RGB is untrustworthy - replace with the
    // averaged color from nearby opaque texels (high mip).  The lower
    // the alpha the more we distrust the original color.
    if (alphaTest != 0 && texColor.a > 0.01 && texColor.a < 1.0) {
        vec3 mipColor = textureLod(uTexture, TexCoord, 4.0).rgb;
        // trust = 0 at alpha 0, trust = 1 at alpha ~0.9
        float trust = smoothstep(0.0, 0.9, texColor.a);
        texColor.rgb = mix(mipColor, texColor.rgb, trust);
    }

    float alphaCutoff = 0.5;
    if (alphaTest == 2) {
        alphaCutoff = 0.4;
    } else if (alphaTest == 3) {
        alphaCutoff = 0.25;
    } else if (alphaTest != 0) {
        alphaCutoff = 0.4;
    }
    // Mip-alpha preservation: alpha mips average downward, thinning distant
    // canopies to skeletons. Boost alpha with mip level so perceived leaf
    // density stays constant with distance.
    if (isFoliage && hasTexture != 0) {
        float mip = textureQueryLod(uTexture, TexCoord).x;
        texColor.a *= 1.0 + clamp(mip, 0.0, 4.0) * 0.18;
    }
    if (alphaTest != 0) {
        // Screen-space sharpened alpha: rescale so the cutoff maps to the
        // texel boundary. With MSAA + alpha-to-coverage on the cutout
        // pipeline this dithers the edge band across samples, smoothing
        // leaf silhouettes instead of the old hard binary discard.
        float aGrad = fwidth(texColor.a);
        texColor.a = clamp((texColor.a - alphaCutoff) / max(aGrad, 0.001) * 0.5 + 0.5, 0.0, 1.0);
        if (texColor.a < 1.0 / 255.0) discard;
    }
    if (colorKeyBlack != 0) {
        float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        if (lum < colorKeyThreshold) discard;
    }
    if (blendMode == 1 && texColor.a < 0.004) discard;

    // Original client sky M2s carry their authored color and alpha. They are
    // camera-centered, unlit, and must not be swallowed by world-distance fog.
    if (vSkyMode != 0) {
        outColor = vec4(texColor.rgb, texColor.a * vFadeAlpha);
        return;
    }

    // Per-instance color variation (foliage only)
    if (isFoliage) {
        float hash = fract(sin(dot(InstanceOrigin.xy, vec2(127.1, 311.7))) * 43758.5453);
        float hueShiftR = 1.0 + (hash - 0.5) * 0.16;       // ±8% red
        float hueShiftB = 1.0 + (fract(hash * 7.13) - 0.5) * 0.16; // ±8% blue
        float brightness = 0.85 + hash * 0.30;               // 85–115%
        texColor.rgb *= vec3(hueShiftR, 1.0, hueShiftB) * brightness;
    }

    vec3 norm = normalize(Normal);
    bool foliageTwoSided = (alphaTest == 2);
    if (!foliageTwoSided && !gl_FrontFacing) norm = -norm;

    // Detail normal perturbation (foliage only) - UV-based only so wind doesn't cause flicker
    if (isFoliage) {
        float nx = sin(TexCoord.x * 12.0 + TexCoord.y * 5.3) * 0.10;
        float ny = sin(TexCoord.y * 14.0 + TexCoord.x * 4.7) * 0.10;
        norm = normalize(norm + vec3(nx, ny, 0.0));
    }

    vec3 ldir = normalize(-lightDir.xyz);
        float nDotL = dot(norm, ldir);
        float diff = foliageTwoSided ? abs(nDotL) : max(nDotL, 0.0);

    vec3 result;
    if (unlit != 0) {
        result = texColor.rgb * emissiveBoost;
        if (emissiveBoost > 1.0) {
            result += vec3(0.32, 0.14, 0.025) * (emissiveBoost - 1.0);
        }
    } else {
        vec3 viewDir = normalize(viewPos.xyz - FragPos);

        float spec = 0.0;
        float shadow = 1.0;
        if (!isFoliage) {
            vec3 halfDir = normalize(ldir + viewDir);
            spec = pow(max(dot(norm, halfDir), 0.0), 32.0) * specularIntensity;
        }

        if (shadowParams.x > 0.5) {
            float normalOffset = SHADOW_TEXEL * 2.0 * (1.0 - abs(dot(norm, ldir)));
            vec3 biasedPos = FragPos + norm * normalOffset;
            vec4 lsPos = lightSpaceMatrix * vec4(biasedPos, 1.0);
            vec3 proj = lsPos.xyz / lsPos.w;
            proj.xy = proj.xy * 0.5 + 0.5;
            if (proj.x >= 0.0 && proj.x <= 1.0 &&
                proj.y >= 0.0 && proj.y <= 1.0 &&
                proj.z >= 0.0 && proj.z <= 1.0) {
                float bias = max(0.0005 * (1.0 - abs(dot(norm, ldir))), 0.00005);
                shadow = sampleShadowPCF(uShadowMap, vec3(proj.xy, proj.z - bias));
            }
            shadow = mix(1.0, shadow, shadowParams.y);
        }

        // Leaf subsurface scattering (foliage only) - uses stable normal, no FragPos dependency
        vec3 sss = vec3(0.0);
        if (isFoliage) {
            float backLit = max(-nDotL, 0.0);
            float viewDotLight = max(dot(viewDir, -ldir), 0.0);
            float sssAmount = backLit * pow(viewDotLight, 4.0) * 0.35 * texColor.a;
            sss = sssAmount * vec3(1.0, 0.9, 0.5) * lightColor.rgb;
        }

        // Sky-bounce ambient for foliage: upward-facing leaves catch more
        // ambient than the canopy underside, giving the crown depth instead
        // of a uniformly-lit blob.
        vec3 ambientTerm = ambientColor.rgb;
        if (isFoliage) {
            ambientTerm *= 0.82 + 0.30 * clamp(norm.z, 0.0, 1.0);
        }
        result = ambientTerm * texColor.rgb
               + shadow * (diff * lightColor.rgb * texColor.rgb + spec * lightColor.rgb)
               + sss;

        if (interiorDarken > 0.0) {
            result *= mix(1.0, 0.5, interiorDarken);
        }
    }

    // Canopy ambient occlusion (foliage only)
    if (isFoliage) {
        float normalizedHeight = clamp(ModelHeight / 18.0, 0.0, 1.0);
        float aoFactor = mix(0.55, 1.0, smoothstep(0.0, 0.6, normalizedHeight));
        result *= aoFactor;
    }

    if (unlit == 0) result += localLightContribution(FragPos, norm, texColor.rgb);

    float dist = length(viewPos.xyz - FragPos);
    float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
    result = mix(fogColor.rgb, result, fogFactor);

    float outAlpha = texColor.a * vFadeAlpha;
    // Cutout materials output the sharpened coverage alpha computed above -
    // alpha-to-coverage turns it into per-sample coverage for smooth edges.
    // Color-key-only materials have no meaningful texture alpha; keep them
    // opaque after the discard.
    if (colorKeyBlack != 0 && alphaTest == 0) {
        outAlpha = vFadeAlpha;
    }
    outColor = vec4(result, outAlpha);
}

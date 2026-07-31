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

layout(push_constant) uniform Push {
    mat4 model;
    float waveAmp;
    float waveFreq;
    float waveSpeed;
    float liquidBasicType;
    vec2 screenSize;  // size of the target being drawn into
} push;

layout(set = 1, binding = 0) uniform WaterMaterial {
    vec4 waterColor;
    float waterAlpha;
    float shimmerStrength;
    float alphaScale;
};

layout(set = 2, binding = 0) uniform sampler2D SceneColor;
layout(set = 2, binding = 1) uniform sampler2D SceneDepth;
layout(set = 2, binding = 2) uniform sampler2D ReflectionColor;
layout(set = 2, binding = 3) uniform ReflectionData {
    mat4 reflViewProj;
};

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) in float WaveOffset;
layout(location = 4) in vec2 ScreenUV;

layout(location = 0) out vec4 outColor;

// ============================================================
// Dual-scroll detail normals (multi-octave ripple overlay)
// ============================================================
vec3 dualScrollWaveNormal(vec2 p, float time) {
    // Three wave octaves at different angles, frequencies, and speeds.
    // Directions are non-axis-aligned to prevent visible tiling patterns.
    // Frequency increases and amplitude decreases per octave (standard
    // multi-octave noise layering for natural water appearance).
    vec2 d1 = normalize(vec2(0.86, 0.51));   // ~30° from +X
    vec2 d2 = normalize(vec2(-0.47, 0.88));  // ~118° (opposing cross-wave)
    vec2 d3 = normalize(vec2(0.32, -0.95));  // ~-71° (third axis for variety)
    float f1 = 0.19, f2 = 0.43, f3 = 0.72;  // spatial frequency (higher = tighter ripples)
    float s1 = 0.95, s2 = 1.73, s3 = 2.40;  // scroll speed (higher octaves move faster)
    float a1 = 0.22, a2 = 0.10, a3 = 0.05;  // amplitude (decreasing for natural falloff)

    vec2 p1 = p + d1 * (time * s1 * 4.0);
    vec2 p2 = p + d2 * (time * s2 * 4.0);
    vec2 p3 = p + d3 * (time * s3 * 4.0);

    float c1 = cos(dot(p1, d1) * f1);
    float c2 = cos(dot(p2, d2) * f2);
    float c3 = cos(dot(p3, d3) * f3);

    float dHx = c1 * d1.x * f1 * a1 + c2 * d2.x * f2 * a2 + c3 * d3.x * f3 * a3;
    float dHy = c1 * d1.y * f1 * a1 + c2 * d2.y * f2 * a2 + c3 * d3.y * f3 * a3;

    return normalize(vec3(-dHx, -dHy, 1.0));
}

// ============================================================
// GGX/Cook-Torrance BRDF
// ============================================================
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom + 1e-7);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    return ggx1 * ggx2;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================
// Linearize depth
// ============================================================
float linearizeDepth(float d, float near, float far) {
    return near * far / (far - d * (far - near));
}

// ============================================================
// Noise functions for foam
// ============================================================
float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float hash22x(vec2 p) {
    return fract(sin(dot(p, vec2(269.5, 183.3))) * 43758.5453);
}

float noiseValue(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbmNoise(vec2 p, float time) {
    float v = 0.0;
    v += noiseValue(p * 3.0 + time * 0.3) * 0.5;
    v += noiseValue(p * 6.0 - time * 0.5) * 0.25;
    v += noiseValue(p * 12.0 + time * 0.7) * 0.125;
    return v;
}

// Voronoi-like cellular noise for foam particles
// jitter parameter controls how much cell points deviate from grid centers
// (0.0 = regular grid, 1.0 = fully random within cell)
float cellularFoam(vec2 p, float jitter) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float minDist = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 cellId = i + neighbor;
            // Jittered cell point — higher jitter = more irregular placement
            vec2 point = vec2(hash21(cellId), hash22x(cellId)) * jitter
                       + vec2(0.5) * (1.0 - jitter);
            float d = length(neighbor + point - f);
            minDist = min(minDist, d);
        }
    }
    return minDist;
}
float cellularFoam(vec2 p) { return cellularFoam(p, 1.0); }

void main() {
    float time = fogParams.z;
    float basicType = push.liquidBasicType;

    // ============================================================
    // Magma / Slime — self-luminous flowing surfaces, skip water path
    // ============================================================
    if (basicType > 1.5) {
        float dist = length(viewPos.xyz - FragPos);
        vec2 flowUV = FragPos.xy;

        bool isMagma = basicType < 2.5;

        // Multi-octave flowing noise for organic lava look
        float n1 = fbmNoise(flowUV * 0.06 + vec2(time * 0.02, time * 0.03), time * 0.4);
        float n2 = fbmNoise(flowUV * 0.10 + vec2(-time * 0.015, time * 0.025), time * 0.3);
        float n3 = noiseValue(flowUV * 0.25 + vec2(time * 0.04, -time * 0.02));
        float flow = n1 * 0.45 + n2 * 0.35 + n3 * 0.20;

        // Dark crust vs bright molten core
        vec3 crustColor, hotColor, coreColor;
        if (isMagma) {
            crustColor = vec3(0.15, 0.04, 0.01);   // dark cooled rock
            hotColor   = vec3(1.0, 0.45, 0.05);     // orange molten
            coreColor  = vec3(1.0, 0.85, 0.3);      // bright yellow-white core
        } else {
            crustColor = vec3(0.05, 0.15, 0.02);
            hotColor   = vec3(0.3, 0.8, 0.15);
            coreColor  = vec3(0.5, 1.0, 0.3);
        }

        // Three-tier color: crust → molten → hot core
        float crustMask = smoothstep(0.25, 0.50, flow);
        float coreMask  = smoothstep(0.60, 0.80, flow);
        vec3 color = mix(crustColor, hotColor, crustMask);
        color = mix(color, coreColor, coreMask);

        // Subtle pulsing emissive glow
        float pulse = 1.0 + 0.15 * sin(time * 1.5 + flow * 6.0);
        color *= pulse;

        // Emissive brightening for hot areas
        color *= 1.0 + coreMask * 0.6;

        float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
        color = mix(fogColor.rgb, color, fogFactor);
        outColor = vec4(color, 0.97);
        return;
    }

    // Derived from the render target, NOT from textureSize(SceneColor): the
    // refraction copy is kept at half resolution, so its size would put every
    // screen-space lookup at twice the intended UV.
    vec2 screenUV = gl_FragCoord.xy / max(push.screenSize, vec2(1.0));

    float dist = length(viewPos.xyz - FragPos);

    // --- Normal computation ---
    vec3 meshNorm = normalize(Normal);
    vec3 detailNorm = dualScrollWaveNormal(FragPos.xy, time);
    // Wave detail is a scrolling pattern with a finite period. Held at full
    // strength to the horizon it does two things the eye picks up immediately:
    // the tiling repeats visibly, and the highlights land smaller than a pixel
    // and alias into shimmer. Fade the detail back toward the flat surface
    // normal with distance so what is left far away is a smooth sheet for the
    // fog to take, which is also where the specular sparkle goes quiet.
    float detailFade = 1.0 - smoothstep(250.0, 1400.0, dist);
    vec3 norm = normalize(mix(meshNorm, detailNorm, 0.55 * detailFade));

    // Player interaction ripple normal perturbation
    vec2 playerPos = vec2(shadowParams.z, shadowParams.w);
    float rippleStrength = fogParams.w;
    float d = length(FragPos.xy - playerPos);
    float rippleEnv = rippleStrength * exp(-d * 0.12);
    if (rippleEnv > 0.001) {
        vec2 radialDir = (FragPos.xy - playerPos) / max(d, 0.01);
        float dHdr = rippleEnv * 0.12 * (-0.12 * sin(d * 2.5 - time * 6.0) + 2.5 * cos(d * 2.5 - time * 6.0));
        norm = normalize(norm + vec3(-radialDir * dHdr, 0.0));
    }

    vec3 viewDir = normalize(viewPos.xyz - FragPos);
    vec3 ldir = normalize(-lightDir.xyz);
    float NdotV = max(dot(norm, viewDir), 0.001);
    float NdotL = max(dot(norm, ldir), 0.0);

    // --- Schlick Fresnel ---
    const vec3 F0 = vec3(0.02);
    float fresnel = F0.x + (1.0 - F0.x) * pow(1.0 - NdotV, 5.0);

    // ============================================================
    // Refraction (screen-space from scene history)
    // ============================================================
    vec2 refractOffset = norm.xy * (0.02 + 0.03 * fresnel);
    vec2 refractUV = clamp(screenUV + refractOffset, vec2(0.001), vec2(0.999));
    // The capture is taken after the scene is drawn and before the water goes
    // over it, so it holds no water and has had no display post-processing
    // applied yet. That means a single sharp tap is correct: there is no
    // feedback loop to smear out, and no baked-in brightness to divide back off.
    vec3 sceneRefract = texture(SceneColor, refractUV).rgb;

    float sceneDepth = texture(SceneDepth, refractUV).r;

    float near = 0.05;
    float far = 30000.0;
    float sceneLinDepth = linearizeDepth(sceneDepth, near, far);
    float waterLinDepth = linearizeDepth(gl_FragCoord.z, near, far);
    float depthDiff = max(sceneLinDepth - waterLinDepth, 0.0);

    // Convert screen-space depth difference to approximate vertical water depth.
    // depthDiff is along the view ray; multiply by the vertical component of
    // the view direction so grazing angles don't falsely trigger shoreline foam
    // on occluding geometry (bridges, posts) that isn't at the waterline.
    float verticalFactor = abs(viewDir.z);  // 1.0 looking straight down, ~0 at grazing
    float verticalDepth = depthDiff * max(verticalFactor, 0.05);

    // ============================================================
    // Beer-Lambert absorption
    // ============================================================
    vec3 absorptionCoeff = vec3(0.46, 0.09, 0.06);
    if (basicType > 0.5 && basicType < 1.5) {
        absorptionCoeff = vec3(0.35, 0.06, 0.04);
    }
    vec3 absorbed = exp(-absorptionCoeff * verticalDepth);

    // Underwater blue fog — geometry below the waterline fades to a blue haze
    // with depth, masking occlusion edge artifacts and giving a natural look.
    vec3 underwaterFogColor = waterColor.rgb * 0.5 + vec3(0.04, 0.10, 0.20);
    float underwaterFogFade = 1.0 - exp(-verticalDepth * 0.35);
    vec3 foggedScene = mix(sceneRefract, underwaterFogColor, underwaterFogFade);

    vec3 shallowColor = waterColor.rgb * 1.2;
    vec3 deepColor = waterColor.rgb * vec3(0.3, 0.5, 0.7);
    float depthFade = 1.0 - exp(-verticalDepth * 0.15);
    vec3 waterBody = mix(shallowColor, deepColor, depthFade);

    // Detect if scene history is available (scene data captured for refraction)
    float sceneBrightness = dot(sceneRefract, vec3(0.299, 0.587, 0.114));
    bool hasSceneData = (sceneBrightness > 0.003);

    // Animated caustic shimmer — only without refraction (refraction already provides movement)
    if (!hasSceneData) {
        float caustic1 = noiseValue(FragPos.xy * 1.8 + time * vec2(0.3, 0.15));
        float caustic2 = noiseValue(FragPos.xy * 3.2 - time * vec2(0.2, 0.35));
        float causticPattern = caustic1 * 0.6 + caustic2 * 0.4;
        vec3 causticTint = vec3(0.08, 0.18, 0.28) * smoothstep(0.35, 0.75, causticPattern);
        waterBody += causticTint;
    }

    vec3 refractedColor;
    if (hasSceneData) {
        refractedColor = mix(foggedScene * absorbed, waterBody, depthFade * 0.7);
        if (verticalDepth < 0.01) {
            float opticalDepth = 1.0 - exp(-dist * 0.004);
            refractedColor = mix(foggedScene, waterBody, opticalDepth * 0.6);
        }
    } else {
        // No refraction data — use lit water body with animated variation
        vec3 litWater = waterBody * (ambientColor.rgb * 0.8 + NdotL * lightColor.rgb * 0.6);
        float normalShift = dot(detailNorm.xy, vec2(0.5, 0.5));
        litWater += vec3(0.02, 0.06, 0.10) * normalShift;
        refractedColor = litWater;
    }

    vec3 litBase = waterBody * (ambientColor.rgb * 0.7 + NdotL * lightColor.rgb * 0.5);
    refractedColor = mix(refractedColor, litBase, clamp(depthFade * 0.3, 0.0, 0.5));

    // ============================================================
    // Planar reflection — subtle, not mirror-like
    // ============================================================
    // reflWeight starts at 0; only contributes where we have valid reflection data
    float reflAmount = 0.0;
    vec3 envReflect = vec3(0.0);

    vec4 reflClip = reflViewProj * vec4(FragPos, 1.0);
    if (reflClip.w > 0.1) {
        vec2 reflUV = reflClip.xy / reflClip.w * 0.5 + 0.5;
        reflUV.y = 1.0 - reflUV.y;
        reflUV += norm.xy * 0.015;

        // Wide fade so there's no visible boundary — fully gone well inside the edge
        float edgeFade = smoothstep(0.0, 0.15, reflUV.x) * smoothstep(1.0, 0.85, reflUV.x)
                       * smoothstep(0.0, 0.15, reflUV.y) * smoothstep(1.0, 0.85, reflUV.y);

        reflUV = clamp(reflUV, vec2(0.002), vec2(0.998));
        vec3 texReflect = texture(ReflectionColor, reflUV).rgb;

        float reflBrightness = dot(texReflect, vec3(0.299, 0.587, 0.114));
        float reflValidity = smoothstep(0.002, 0.05, reflBrightness) * edgeFade;

        envReflect = texReflect * 0.5;
        reflAmount = reflValidity * 0.4;
    }

    // ============================================================
    // GGX Specular
    // ============================================================
    float roughness = 0.18;
    vec3 halfDir = normalize(ldir + viewDir);
    float D = DistributionGGX(norm, halfDir, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlickRoughness(max(dot(halfDir, viewDir), 0.0), F0, roughness);
    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.001) * lightColor.rgb * NdotL;
    specular = min(specular, vec3(2.0));

    // Noise-based sparkle. Its features are far smaller than the wave detail, so
    // it aliases sooner — fade it out over the near half of the detail range,
    // otherwise flattening the normals just leaves the sparkle field behind as
    // the visible pattern.
    float sparkleNoise = fbmNoise(FragPos.xy * 4.0 + time * 0.5, time * 1.5);
    float sparkle = pow(max(sparkleNoise - 0.55, 0.0) / 0.45, 3.0) * shimmerStrength * 0.10;
    specular += sparkle * lightColor.rgb * (1.0 - smoothstep(150.0, 700.0, dist));

    // ============================================================
    // Subsurface scattering
    // ============================================================
    float sssBase = pow(max(dot(viewDir, -ldir), 0.0), 4.0);
    float sss = sssBase * max(0.0, WaveOffset * 3.0) * 0.25;
    vec3 sssColor = vec3(0.05, 0.55, 0.35) * sss * lightColor.rgb;

    // ============================================================
    // Combine — reflection only where valid, no dark fallback
    // ============================================================
    // reflAmount is 0 where no valid reflection data exists — no dark arc
    float reflectWeight = clamp(fresnel * reflAmount, 0.0, 0.30);
    vec3 color = mix(refractedColor, envReflect, reflectWeight);
    color += specular + sssColor;

    float crest = smoothstep(0.5, 1.0, WaveOffset) * 0.04;
    color += vec3(crest);

    // ============================================================
    // Shoreline foam — scattered particles, not smooth bands
    // Only on terrain water (waveAmp > 0); WMO water (canals, indoor)
    // has waveAmp == 0 and should not show shoreline interaction.
    // ============================================================
    if (basicType < 1.5 && verticalDepth > 0.01 && push.waveAmp > 0.0) {
        float foamDepthMask = 1.0 - smoothstep(0.0, 1.8, verticalDepth);

        // Warp UV coords with noise to break up cellular regularity
        vec2 warpOffset = vec2(
            noiseValue(FragPos.xy * 2.5 + time * 0.08) - 0.5,
            noiseValue(FragPos.xy * 2.5 + vec2(37.0) + time * 0.06) - 0.5
        ) * 1.6;
        vec2 foamUV = FragPos.xy + warpOffset;

        // Fine scattered particles
        float cells1 = cellularFoam(foamUV * 14.0 + time * vec2(0.15, 0.08));
        float foam1 = (1.0 - smoothstep(0.0, 0.12, cells1)) * 0.45;

        // Tiny spray dots
        float cells2 = cellularFoam(foamUV * 28.0 + time * vec2(-0.12, 0.22));
        float foam2 = (1.0 - smoothstep(0.0, 0.07, cells2)) * 0.3;

        // Micro specks
        float cells3 = cellularFoam(foamUV * 50.0 + time * vec2(0.25, -0.1));
        float foam3 = (1.0 - smoothstep(0.0, 0.05, cells3)) * 0.18;

        // Noise breakup for clumping
        float noiseMask = noiseValue(FragPos.xy * 3.0 + time * 0.15);
        float foam = (foam1 + foam2 + foam3) * foamDepthMask * smoothstep(0.3, 0.6, noiseMask);

        foam *= smoothstep(0.0, 0.1, verticalDepth);
        // Bluer foam tint instead of near-white
        color = mix(color, vec3(0.68, 0.78, 0.88), clamp(foam, 0.0, 0.40));
    }

    // ============================================================
    // Wave crest foam (ocean only) — particle-based
    // ============================================================
    if (basicType > 0.5 && basicType < 1.5 && push.waveAmp > 0.0) {
        float crestMask = smoothstep(0.5, 1.0, WaveOffset);
        vec2 crestWarp = vec2(
            noiseValue(FragPos.xy * 1.8 + time * 0.1) - 0.5,
            noiseValue(FragPos.xy * 1.8 + vec2(53.0) + time * 0.07) - 0.5
        ) * 2.0;
        float crestCells = cellularFoam((FragPos.xy + crestWarp) * 6.0 + time * vec2(0.12, 0.08));
        float crestFoam = (1.0 - smoothstep(0.0, 0.18, crestCells)) * crestMask;
        float crestNoise = noiseValue(FragPos.xy * 3.0 - time * 0.3);
        crestFoam *= smoothstep(0.3, 0.6, crestNoise);
        color = mix(color, vec3(0.68, 0.78, 0.88), crestFoam * 0.30);
    }

    // ============================================================
    // Alpha and fog
    // ============================================================
    float baseAlpha = mix(waterAlpha, min(1.0, waterAlpha * 1.5), depthFade);
    float alpha = mix(baseAlpha, min(1.0, baseAlpha * 1.3), fresnel) * alphaScale;
    alpha = clamp(alpha, 0.15, 0.92);
    // Dissolve the sheet before the water geometry runs out, so the ocean fades
    // into the horizon haze instead of ending on a hard line. This has to come
    // after the clamp — clamping afterwards restored the 0.15 floor and put the
    // edge straight back.
    alpha *= smoothstep(2400.0, 600.0, dist);

    float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
    color = mix(fogColor.rgb, color, fogFactor);

    outColor = vec4(color, alpha);
}

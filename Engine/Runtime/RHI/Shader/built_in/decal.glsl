#version 450 core

// Decal3D's paint pass. The geometry arrives already in world space and already clipped to the
// decal's volume by the CPU projection pass (see DecalPatchUVE), so the vertex stage only projects
// it and the fragment stage only asks how much of the decal reaches this pixel.
//
// The fades are re-evaluated here, per pixel, from the authored fields: the CPU evaluates the same
// three fades at each patch's centre to decide whether the patch paints at all, but a patch is one
// receiving face and a wall's face can span the whole volume - a weight flat across it would end in
// a hard rectangle where the artist asked for a fade. The rules below mirror
// Scene::SampleDecal3DUVE() exactly; the coordinate space they read is not mirrored, it is the same
// matrix the CPU clipped in (DecalDrawCommandUVE::worldToUnit).

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;

uniform mat4 uViewProjection;

void main() {
    vWorldPosition = aPosition;
    vNormal = aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform vec3 uViewPosition;
uniform sampler2D uAlbedoTexture;
uniform mat4 uWorldToUnit;
uniform vec3 uProjectionDirection;
uniform vec3 uBaseColor;
uniform vec3 uEmissionColor;
uniform float uAlphaScale;
uniform float uNormalFade;
uniform float uUpperFade;
uniform float uLowerFade;
uniform float uDistanceFadeEnabled;
uniform float uDistanceFadeBegin;
uniform float uDistanceFadeLength;

// Scene::SampleDecal3DUVE()'s helper of the same name: 0 leaves the axis unfaded, 1 fades the whole
// half of the volume, and anything between fades the outer band of it.
float AxisFadeWeight(float unitCoordinate, float band) {
    if (!(band > 0.0)) {
        return 1.0;
    }
    float edge = 1.0 - band;
    if (unitCoordinate <= edge) {
        return 1.0;
    }
    return 1.0 - min((unitCoordinate - edge) / band, 1.0);
}

void main() {
    // The volume's unit coordinates at this pixel: [-1, 1] on each axis, 0 at the volume's centre.
    // The patches are inside it by construction, so "inside" is not re-tested here.
    vec3 local = (uWorldToUnit * vec4(vWorldPosition, 1.0)).xyz;

    // How squarely the surface faces the decal: a surface turned back up the projection direction is
    // fully facing, an edge-on sliver or a backface scores zero.
    float facingTowardsDecal = clamp(dot(normalize(vNormal), -uProjectionDirection), 0.0, 1.0);
    float normalWeight = 1.0 - clamp(uNormalFade, 0.0, 1.0) * (1.0 - facingTowardsDecal);

    // The volume's +Y end is the far end of the projection, each authored band fades its own half.
    float depthWeight = AxisFadeWeight(local.y, uUpperFade) * AxisFadeWeight(-local.y, uLowerFade);

    float distanceWeight = 1.0;
    if (uDistanceFadeEnabled > 0.5) {
        float cameraDistance = distance(uViewPosition, vWorldPosition);
        distanceWeight = uDistanceFadeLength > 0.0
                             ? 1.0 - clamp((cameraDistance - uDistanceFadeBegin) / uDistanceFadeLength, 0.0, 1.0)
                             : (cameraDistance <= uDistanceFadeBegin ? 1.0 : 0.0);
    }

    float weight = clamp(normalWeight * depthWeight * distanceWeight, 0.0, 1.0);

    // The material's texture, sampled with the patch's unit coordinates, and its alpha is what
    // makes a decal the shape the artist drew rather than the rectangle it was clipped to. The
    // renderer binds a 1x1 white texture when the material leaves albedo unset, so an untextured
    // decal paints its flat colour and takes this alpha as 1.
    vec4 albedo = texture(uAlbedoTexture, vTexCoord);
    FragColor = vec4((uBaseColor + uEmissionColor) * albedo.rgb, albedo.a * weight * clamp(uAlphaScale, 0.0, 1.0));
}
#endif

#version 450

/* Deterministic Cornell-box ray-casting demo.
 * Fixed area-light strata provide clean soft shadows without Monte Carlo noise. */
layout(push_constant) uniform RayParams {
    float timeSeconds;
    float width;
    float height;
} params;

layout(location = 0) out vec4 outColor;

const float ROOM_X = 2.5;
const float FLOOR_Y = -1.5;
const float CEILING_Y = 2.5;
const float BACK_Z = -2.5;

struct Hit {
    float t;
    vec3 normal;
    int material; // 0 white, 1 red, 2 green, 3 box, 4 glass, 5 light
};

bool hitSphere(vec3 ro, vec3 rd, vec3 center, float radius, out float t) {
    vec3 oc = ro - center;
    float halfB = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float d = halfB * halfB - c;
    if (d < 0.0) return false;
    float root = sqrt(d);
    t = -halfB - root;
    if (t <= 0.001) t = -halfB + root;
    return t > 0.001;
}

bool hitBox(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, out float t, out vec3 normal) {
    vec3 invD = 1.0 / rd;
    vec3 t0 = (bmin - ro) * invD;
    vec3 t1 = (bmax - ro) * invD;
    vec3 tsmaller = min(t0, t1);
    vec3 tbigger = max(t0, t1);
    float nearT = max(max(tsmaller.x, tsmaller.y), tsmaller.z);
    float farT = min(min(tbigger.x, tbigger.y), tbigger.z);
    if (farT < max(nearT, 0.001)) return false;
    t = nearT > 0.001 ? nearT : farT;
    vec3 p = ro + rd * t;
    vec3 d = min(abs(p - bmin), abs(p - bmax));
    if (d.x < d.y && d.x < d.z) normal = vec3(p.x > (bmin.x+bmax.x)*0.5 ? 1 : -1,0,0);
    else if (d.y < d.z) normal = vec3(0,p.y > (bmin.y+bmax.y)*0.5 ? 1 : -1,0);
    else normal = vec3(0,0,p.z > (bmin.z+bmax.z)*0.5 ? 1 : -1);
    return true;
}

void consider(inout Hit hit, float t, vec3 normal, int material) {
    if (t > 0.001 && t < hit.t) {
        hit.t = t;
        hit.normal = normal;
        hit.material = material;
    }
}

Hit intersectScene(vec3 ro, vec3 rd, bool includeGlass) {
    Hit hit;
    hit.t = 1e20;
    hit.normal = vec3(0,1,0);
    hit.material = -1;
    float t;
    vec3 p;

    if (abs(rd.y) > 1e-6) {
        t = (FLOOR_Y-ro.y)/rd.y; p = ro+rd*t;
        if (p.x >= -ROOM_X && p.x <= ROOM_X && p.z >= BACK_Z && p.z <= 7.0)
            consider(hit,t,vec3(0,1,0),0);
        t = (CEILING_Y-ro.y)/rd.y; p = ro+rd*t;
        if (p.x >= -ROOM_X && p.x <= ROOM_X && p.z >= BACK_Z && p.z <= 7.0)
            consider(hit,t,vec3(0,-1,0),0);
        /* Emissive panel sits just below the ceiling. */
        t = (2.47-ro.y)/rd.y; p = ro+rd*t;
        if (p.x > -0.62 && p.x < 0.62 && p.z > -1.55 && p.z < -0.72)
            consider(hit,t,vec3(0,-1,0),5);
    }
    if (abs(rd.z) > 1e-6) {
        t = (BACK_Z-ro.z)/rd.z; p = ro+rd*t;
        if (p.x >= -ROOM_X && p.x <= ROOM_X && p.y >= FLOOR_Y && p.y <= CEILING_Y)
            consider(hit,t,vec3(0,0,1),0);
    }
    if (abs(rd.x) > 1e-6) {
        t = (-ROOM_X-ro.x)/rd.x; p = ro+rd*t;
        if (p.y >= FLOOR_Y && p.y <= CEILING_Y && p.z >= BACK_Z && p.z <= 7.0)
            consider(hit,t,vec3(1,0,0),2);
        t = (ROOM_X-ro.x)/rd.x; p = ro+rd*t;
        if (p.y >= FLOOR_Y && p.y <= CEILING_Y && p.z >= BACK_Z && p.z <= 7.0)
            consider(hit,t,vec3(-1,0,0),1);
    }

    vec3 boxMin = vec3(-1.45,FLOOR_Y,-2.12);
    vec3 boxMax = vec3(-0.18,0.20,-0.86);
    vec3 n;
    if (hitBox(ro,rd,boxMin,boxMax,t,n)) consider(hit,t,n,3);

    if (includeGlass) {
        vec3 sphereCenter = vec3(0.72,-0.72,-1.45);
        if (hitSphere(ro,rd,sphereCenter,0.76,t)) {
            vec3 sn = normalize(ro+rd*t-sphereCenter);
            consider(hit,t,sn,4);
        }
    }
    return hit;
}

float lightVisibility(vec3 point, vec3 lightPoint, float maxDistance) {
    vec3 delta = lightPoint-point;
    float distance = length(delta);
    vec3 rd = delta/max(distance,1e-5);
    float t; vec3 n;
    if (hitBox(point+rd*0.002,rd,vec3(-1.45,FLOOR_Y,-2.12),
               vec3(-0.18,0.20,-0.86),t,n) && t < maxDistance-0.01) return 0.0;
    /* Approximate thin dielectric transmission: the glass sphere dims the
     * light through it instead of producing a fully opaque shadow. */
    if (hitSphere(point+rd*0.002,rd,vec3(0.72,-0.72,-1.45),0.76,t) &&
        t < maxDistance-0.01) return 0.52;
    return 1.0;
}

vec3 materialColor(int material) {
    if (material == 1) return vec3(0.72,0.075,0.055);
    if (material == 2) return vec3(0.075,0.48,0.12);
    if (material == 3) return vec3(0.22,0.20,0.19);
    return vec3(0.73,0.72,0.68);
}

/* Keep the software fallback on the same Phong model as the Vulkan
 * ray-query path: ambient + Lambert diffuse + reflected-vector specular. */
void phongMaterial(int material, out vec3 ka, out vec3 kd, out vec3 ks, out float shininess) {
    ka = vec3(0.055);
    kd = materialColor(material);
    ks = vec3(0.035);
    shininess = 20.0;
    if (material == 3) { ks = vec3(0.12); shininess = 32.0; }
    else if (material == 1 || material == 2) { ks = vec3(0.07); shininess = 28.0; }
}

vec3 shade(vec3 ro, vec3 rd) {
    Hit hit = intersectScene(ro,rd,true);
    if (hit.material < 0) return vec3(0.015,0.018,0.022);
    if (hit.material == 5) return vec3(14.0,13.5,12.5);

    vec3 point = ro+rd*hit.t;
    vec3 normal = normalize(hit.normal);
    if (dot(normal,rd) > 0.0) normal = -normal;

    /* Deterministic strata keep soft shadows stable and noise-free. */
    const int GRID = 8;
    vec3 ka, kd, ks;
    float shininess;
    phongMaterial(hit.material,ka,kd,ks,shininess);
    vec3 direct = vec3(0.0);
    vec3 viewDirection = normalize(-rd);
    for (int y=0; y<GRID; ++y) for (int x=0; x<GRID; ++x) {
        vec2 uv = (vec2(x,y)+vec2(0.5))/float(GRID);
        vec3 lightPoint = vec3(mix(-0.55,0.55,uv.x),2.465,mix(-1.48,-0.79,uv.y));
        vec3 toLight = lightPoint-point;
        float distance2 = dot(toLight,toLight);
        float distance = sqrt(distance2);
        vec3 lightDir = toLight/max(distance,1e-5);
        float ndotl = max(dot(normal,lightDir),0.0);
        if (ndotl > 0.0) {
            float visibility = lightVisibility(point+normal*0.003,lightPoint,distance);
            if (visibility <= 0.0) continue;
            vec3 radiance = vec3(1.0,0.96,0.88)/(1.0+0.16*distance2);
            float specular = max(dot(reflect(-lightDir,normal),viewDirection),0.0);
            direct += (kd*ndotl+ks*pow(specular,shininess))*radiance*visibility;
        }
    }
    direct /= float(GRID*GRID);

    if (hit.material == 4) {
        /* Trace one deterministic refracted path through the dielectric sphere.
         * This lets the room and box remain visible, unlike a flat grey sphere. */
        const vec3 sphereCenter = vec3(0.72,-0.72,-1.45);
        const float sphereRadius = 0.76;
        const float ior = 1.50;
        vec3 insideDir = refract(rd,normal,1.0/ior);
        vec3 transmitted = vec3(0.66,0.78,0.82);
        float exitT;
        if (dot(insideDir,insideDir) > 1e-6 &&
            hitSphere(point+insideDir*0.002,insideDir,sphereCenter,sphereRadius,exitT)) {
            vec3 exitPoint = point+insideDir*exitT;
            vec3 exitNormal = normalize(exitPoint-sphereCenter);
            vec3 outsideDir = refract(insideDir,-exitNormal,ior);
            if (dot(outsideDir,outsideDir) > 1e-6) {
                Hit behind = intersectScene(exitPoint+outsideDir*0.003,normalize(outsideDir),false);
                if (behind.material >= 0 && behind.material != 5) {
                    vec3 behindColor = materialColor(behind.material);
                    float behindLight = 0.32+0.68*max(dot(normalize(behind.normal),
                        normalize(vec3(-0.25,0.85,0.30))),0.0);
                    transmitted = behindColor*behindLight;
                }
            }
        }

        /* Schlick Fresnel for an air/glass interface (IOR 1.5 => F0 ~= 0.04). */
        float cosTheta = clamp(dot(-rd,normal),0.0,1.0);
        float fresnel = 0.04+0.96*pow(1.0-cosTheta,5.0);
        vec3 reflectedDir = reflect(rd,normal);
        Hit reflectedHit = intersectScene(point+reflectedDir*0.003,reflectedDir,false);
        vec3 reflected = reflectedHit.material >= 0
            ? (reflectedHit.material == 5 ? vec3(1.0) : materialColor(reflectedHit.material))
            : vec3(0.32,0.42,0.54);
        float highlight = pow(max(dot(reflectedDir,
            normalize(vec3(-0.35,0.88,0.3))),0.0),120.0);
        return mix(transmitted,reflected,fresnel)+vec3(1.0)*highlight*0.75+direct*0.03;
    }

    /* Ambient is intentionally explicit; direct-light samples contain only
     * diffuse and specular terms, so shadows do not erase ambient fill. */
    vec3 ambient = ka*vec3(0.30,0.31,0.34);
    return ambient + direct*4.0;
}

void main() {
    vec2 uv = gl_FragCoord.xy/vec2(params.width,params.height);
    vec2 screen = vec2((uv.x*2.0-1.0)*(params.width/params.height),1.0-uv.y*2.0);
    vec3 ro = vec3(0.0,0.95,7.0);
    vec3 rd = normalize(vec3(screen*1.45,-4.6));
    vec3 color = shade(ro,rd);
    /* Display transform keeps the bright area light from clipping harshly. */
    color = color/(1.0+color);
    outColor = vec4(color,1.0);
}

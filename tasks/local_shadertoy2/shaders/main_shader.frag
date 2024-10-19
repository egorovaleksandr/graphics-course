#version 430
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D iChannel0;
layout(binding = 1) uniform sampler2D iChannel1;

layout(push_constant) uniform params
{
  uvec2 iResolution;
  uvec2 iMouse;
  float iTime;
};

#define             PI 3.14159265359
#define MAX_ITERS   700
#define MAX_DIST    100.0
#define EPS         0.001


struct obj {
    float d;
    int id;
};

float torus ( in vec3 p, in vec2 t )
{
    vec2 q  = vec2 ( length ( p.xz) - t.x, p.y );
    return length ( q ) - t.y;
}

float sphere ( vec3 p, in vec3 center, in float radius )
{
    return length ( p - center ) - radius;
}

float plane ( in vec3 p, vec4 n )
{
    return dot ( p, n.xyz ) + n.w;
}

obj smoothSubtraction ( obj f, obj s, float k ) 
{
    float d1 = f.d;
    float d2 = s.d;
    float h = clamp( 0.5 - 0.5*(d2+d1)/k, 0.0, 1.0 );
    return obj(mix( d2, -d1, h ) + k*h*(1.0-h), f.id); 
}

obj smoothIntersection ( obj f, obj s, float k ) 
{
    float d1 = f.d;
    float d2 = s.d;
    float h = clamp( 0.5 - 0.5*(d2-d1)/k, 0.0, 1.0 );
    return obj(mix( d2, d1, h ) + k*h*(1.0-h), f.id); 
}

obj smoothUnion ( obj f, obj s, float k ) 
{
    float d1 = f.d;
    float d2 = s.d;
    float h = clamp( 0.5 + 0.5*(d2-d1)/k, 0.0, 1.0 );
    return obj(mix( d2, d1, h ) - k*h*(1.0-h), f.id); 
}

obj objmin(obj f, obj s) {
    if (f.d < s.d)
        return f;
    return s;
}

obj sdf(in vec3 p) {
    obj sph1 = obj(sphere ( p, vec3 ( 0.0, 3.4, 0.0 ), 1.0 * (1.0 + sin(iTime + PI * 0.75))) + 0.05 * (1.0 + sin(10.0 *(length(p) + 2.0*iTime))), 0);
    obj sph2 = obj(sphere ( p, vec3 ( 3.4, 0.0, 0.0 ), 1.0 * (1.0 + sin(iTime))) + 0.05 * (1.0 + sin(10.0 *(length(p) + 2.0*iTime))), 0);
    obj sph3 = obj(sphere ( p, vec3 ( 0.0, 0.0, 3.4 ), 1.0 * (1.0 + sin(iTime))) + 0.05 * (1.0 + sin(10.0 *(length(p) + 2.0*iTime))), 0);
    obj sph4 = obj(sphere ( p, vec3 ( 0.0, -3.4, 0.0 ), 1.0 * (1.0 + sin(iTime + PI * 0.75))) + 0.05 * (1.0 + sin(10.0 *(length(p) + 2.0*iTime))), 0);
    obj sph5 = obj(sphere ( p, vec3 ( -3.4, 0.0, 0.0 ), 1.0 * (1.0 + sin(iTime))) + 0.05 * (1.0 + sin(10.0 *(length(p) + 2.0*iTime))), 0);
    obj sph6 = obj(sphere ( p, vec3 ( 0.0, 0.0, -3.4 ), 1.0 * (1.0 + sin(iTime))) + 0.05 * (1.0 + sin(10.0 *(length(p) + 2.0*iTime))), 0);
    
    obj sph_0 = objmin(objmin( sph5, sph2) , objmin( sph3, sph6));
    
    obj sph  = obj(sphere ( p, vec3 ( 0.0, 0.0, 0.0 ), 4.0 ) +  0.025 * cos(2.0 * p.x * p.y * p.z + iTime * 3.0), 0);
    obj pl   = obj(plane(p, vec4 (0.0, 1.0, 0.0, 5.0)) + 0.5 * sin(length(p) - iTime), 1);
    
    obj tor  = obj(torus(p, vec2 (4.0 * (1.0 + sin(1.0 * iTime)), 1)) + 0.01 * (1.0 + sin(15.0 * p.y + 15.0 * p.x + p.z * 15.0 + iTime*5.0)), 2);
    
    return objmin(smoothUnion(smoothSubtraction(objmin(sph1, sph4), smoothSubtraction(sph_0, sph, 1.0), 0.5), tor, 3.0), pl); //objmin(sph, pl);
    //return obj(sphere ( p, vec3 ( 0.0, 0.0, 0.0 ), 5.0) ,0);
}

mat3 rotateX(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(1, 0, 0),
        vec3(0, c, -s),
        vec3(0, s, c)
    );
}

vec3 trace ( vec3 from, vec3 dir, out bool hit, out int steps, out int id, out float d )
{
    vec3     p         = from;
    float    totalDist = 0.0;
    
    hit = false;
    
    for ( steps = 0; steps < MAX_ITERS; steps++ )
    {
        obj    dist = sdf ( p );
        id = dist.id;
        
        if ( dist.d < EPS )
        {
            hit = true;
            break;
        }
        
        totalDist += dist.d;
        
        if ( totalDist > MAX_DIST )
            break;
            
        p += dist.d * dir;
    }
    d = totalDist;
    return p;
}

mat3 rotateY(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(c, 0, s),
        vec3(0, 1, 0),
        vec3(-s, 0, c)
    );
}

mat3 rotateZ(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(c, -s, 0),
        vec3(s, c, 0),
        vec3(0, 0, 1)
    );
}

mat2 rotate2d(in float angle) {
    float angle_cos = cos(angle);
    float angle_sin = sin(angle);
    return mat2(
        vec2(angle_cos, angle_sin),
        vec2(-angle_sin, angle_cos)
    );
}

mat3 camera(in vec3 camera_pos, in vec3 look_at_point) {
    vec3 camera_dir = normalize(look_at_point - camera_pos);
    vec3 camera_right = normalize(cross(vec3(0, 1, 0), camera_dir));
    vec3 camera_up = normalize(cross(camera_dir, camera_right));
    
    return mat3(-camera_right, camera_up, -camera_dir);
}

vec3 generateNormal ( vec3 surfacePoint, float d )
{ 
    float e = d;
    float dx1 = sdf(surfacePoint + vec3(e, 0, 0)).d;
    float dx2 = sdf(surfacePoint - vec3(e, 0, 0)).d;
    float dy1 = sdf(surfacePoint + vec3(0, e, 0)).d;
    float dy2 = sdf(surfacePoint - vec3(0, e, 0)).d;
    float dz1 = sdf(surfacePoint + vec3(0, 0, e)).d;
    float dz2 = sdf(surfacePoint - vec3(0, 0, e)).d;
  
    return normalize ( vec3 ( dx1 - dx2, dy1 - dy2, dz1 - dz2 ) );
}

void main() {
    vec2 fragCoord = vec2(gl_FragCoord).xy;
    vec3 camera_origin = vec3(0.0, 7.0, 0.0);
    float camera_radius = 7.0;
    vec3 look_at_point = vec3(0.0, 1.0, 0.0);

    vec2 uv_coord = -(fragCoord - 0.5 * iResolution.xy) / max(iResolution.x, iResolution.y);
    vec2 mouse = vec2(iResolution.x - iMouse.x, iResolution.y - iMouse.y) / iResolution.xy;

    camera_origin.yz = camera_origin.yz * camera_radius * rotate2d(mix(PI / 2.0, 0.0, mouse.y));
    camera_origin.xz = camera_origin.xz * rotate2d(mix(-PI, PI, mouse.x)) + vec2(look_at_point.x, look_at_point.z);
    
    vec3 ray_dir = camera(camera_origin, look_at_point) * normalize(vec3(uv_coord, -1));
    
    bool hit;
    int id;
    int  steps;
    float d;
    vec3 intersectionPoint = trace (camera_origin, ray_dir, hit, steps, id, d );
    vec3 surfaceNormal  = generateNormal ( intersectionPoint, 0.001);
    vec3 lightSource = vec3  ( -15.0, 25.0, -48.0 );
    if (hit) {
        vec3 lightVector  = normalize ( lightSource - intersectionPoint );
        vec3 surfaceNormal  = generateNormal ( intersectionPoint, 0.001 );
        vec3 n = abs(surfaceNormal);
        vec3 reflected = ray_dir - 2.0 * dot(ray_dir, surfaceNormal) * surfaceNormal;
        float specular = pow(max(dot(reflected, lightVector), 0.0), 32.0);
        vec3 p = intersectionPoint;
        if (id == 0) {
            //vec3 txtr = 1.5 * (n.x * textureLod(iChannel1, p.yz).rgb + n.y * texture(iChannel1, p.xz).rgb + n.z * texture(iChannel1, p.xy).rgb) * (n.x * texture(iChannel2, p.yz).rgb + n.y * texture(iChannel2, p.xz).rgb + n.z * texture(iChannel2, p.xy).rgb);
            vec3 txtr = n.x * textureLod(iChannel0, p.yz, 0).rgb + n.y * textureLod(iChannel0, p.xz, 0).rgb + n.z * textureLod(iChannel0, p.xy, 0).rgb;
            fragColor = vec4 ( 2.19 / 2.0, 1.71 / 2.0, 0.37 / 2.0, 0.0 ) * max(dot(surfaceNormal, lightVector), 0.0) + specular * vec4(1.0, 0.0, 1.0, 0.0) + 0.05;
            fragColor *= vec4(txtr, 0.0);
        } else {
            //vec3 txtr = 1.0 * (n.x * texture(iChannel3, p.yz).rgb + n.y * texture(iChannel3, p.xz).rgb + n.z * texture(iChannel3, p.xy).rgb);
            vec3 txtr = n.x * textureLod(iChannel1, p.yz, 0).rgb + n.y * textureLod(iChannel1, p.xz, 0).rgb + n.z * textureLod(iChannel1, p.xy, 0).rgb;
            fragColor = vec4 ( sin(intersectionPoint.x * 4.0), 0.5 + 0.5  * sin(length(intersectionPoint) - iTime), sin(intersectionPoint.z * 4.0), 0.0 ) * max(dot(surfaceNormal, lightVector), 0.0) * 0.85 + 0.15 + specular * vec4(0.5 + 0.5 * sin(iTime), 0.0,  0.33 + 0.33 * cos(iTime), 0.0);
            fragColor *= vec4(txtr, 0.0);
        }
    } else {
        fragColor = vec4 ( 0.1 , 0.3 , 0.45, 0.0 );
    }
    vec3 col = textureLod(iChannel0, ray_dir.xy, 0).rgb;
    col = mix(col, fragColor.xyz, step((d) - MAX_DIST / 1.01, 0.0));
    fragColor = vec4(col, 1.0);
}
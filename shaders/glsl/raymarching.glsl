float sdTorus( vec3 p, vec2 t )
{
  vec2 q = vec2(length(p.xz)-t.x,p.y);
  return length(q)-t.y;
}

mat2 rot2D(float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c,-s,s,c);
}

// cosine based palette
vec3 palette( in float t)
{
    vec3 a = vec3(0.0, 0.0, 0.0);
    vec3 b = vec3(0.749,0.761,0.059);
    vec3 c = vec3(1.0, 1.0, 1.0);
    vec3 d = vec3(-0.430, -0.397, -0.083);
    return a + b*cos( 6.283185*(c*t+d) );
}

float map( vec3 p)
{
    p.z += iTime * .4;
 
     // Space repetition
    p.xy = (fract(p.xy) - .5);
    p.z = mod(p.z,.25) - .125;
  
    float torus = sdTorus(p * vec3(1,1,.6),vec2(.1,.1)); // torus sdf
  
    return torus;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    // Normalized pixel coordinates (0,1) and making the center of the canvas as (0,0),
    //so the coordinates became (-1,1) and also adjusting for the aspect ratio of the canvas.
    vec2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    vec2 m = (iMouse.xy * 2. - iResolution.xy) / iResolution.y;
    float fov = 1.5; // var to adjust field of view
    
    vec3 ro = vec3(0,0,-3); // ray origin
    vec3 rd = normalize(vec3(uv * fov,1)); // ray direction
    float t = 0.0; // total distance travelled
    
    vec3 col = vec3(0); // final pixel color
    
   if(iMouse.z < 0.) m = vec2(cos(iTime*.2),sin(iTime*.2));
    
    // Raymarching
    int i;
    for(i = 0; i < 80; i++) 
    {
        vec3 p = ro + rd * t; // position along the ray
        
        p.xy *= rot2D(t*.2 *m.x);
        
        p.y += sin(t*(m.y+1.)*.5)*.35;  // wiggle ray
    
        float d = map(p); // current distance to the scene
    
        t += d; // march the ray
        
        if(d < .001 || t > 100.) break;
    }
    
    col = palette(t *.04 + float(i)*.005);
    
    // Output to screen
    fragColor = vec4(col,1.0);
}
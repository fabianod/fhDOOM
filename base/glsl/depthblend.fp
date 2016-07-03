#include "global.inc"

layout(binding = 1) uniform sampler2D texture1;
layout(binding = 2) uniform sampler2D texture7;

in vs_output
{
  vec2 texcoord;
  vec3 normal;
  vec3 binormal;
  vec3 tangent;
  vec4 color;
  float depth;
} frag;

float LinearizeDepth(float z) 
{ 
  float n = rpClipRange[0];    // camera z near 
  float f = rpClipRange[1]; // camera z far 
  return (2.0 * n) / (f + n - z * (f - n));
} 


float WorldDepth()
{
  vec2 depthTextureSize = textureSize(texture7, 0);
  vec2 texcoord = gl_FragCoord.xy / depthTextureSize;

  return texture2D(texture7, texcoord).x;
}

float ColorAlphaZero()
{
  float dist = abs( LinearizeDepth(WorldDepth()) - LinearizeDepth(gl_FragCoord.z) ) * rpClipRange[1];
  return clamp(dist / rpDepthBlendRange, 0.0, 1.0);
}

float ColorAlphaOne()
{  
  return 1.0 - ColorAlphaZero();
}

out vec4 result;

void main(void)
{  
  result = texture2D(texture1, frag.texcoord) *  frag.color;// * clamp(rpDiffuseColor, vec4(0,0,0,0), vec4(1,1,1,1));

  switch(rpDepthBlendMode)
  {
  case 0:
  case 1:
  case 2:
    break;
  case 3:
    result *= ColorAlphaZero();
    break;
  case 4:
    result *= ColorAlphaOne();    
    break;
  case 5:
    result.a *= ColorAlphaOne();        
    break;    
  case 6:
    result.a *= ColorAlphaZero();        
    break;        
  default:
    result = vec4(1,0,0,1);
    break;
  }
}

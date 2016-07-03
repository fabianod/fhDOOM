#include "global.inc"

layout(binding = 1) uniform sampler2D texture1;

in vs_output
{
  vec2 texcoord;
} frag;

out vec4 result;


float linearDepth(float expDepth) {
  const float f = 1.0;
  const float n = 0.1;

  float z = (2 * n) / (f + n - expDepth * (f - n));

  return z;
}

void main(void)
{  
  float d = texture2D(texture1, frag.texcoord).x;

  d = linearDepth(d);

  result = vec4(d,d,d,1);
}
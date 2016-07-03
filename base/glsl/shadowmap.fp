#include "global.inc"

layout(binding = 0) uniform sampler2D texture0;

in vs_output
{
  vec2 texcoord;
} fragment;

out vec4 result;

void main(void)
{

  if(rpAlphaTestEnabled != 0)
  {
    vec4 color = texture2D(texture0, fragment.texcoord);
    if(color.a < rpAlphaTestThreshold)
      discard;
  }

  result = rpDiffuseColor;
}
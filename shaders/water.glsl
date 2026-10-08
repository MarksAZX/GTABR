// Shared analytic water displacement and normals; near-shore swells travel toward land.
float waveHeight(vec2 p,float depth,float time){
  float shore=smoothstep(0.0,3.0,depth);
  float scale=mix(0.7,1.0,g.effectsInfo.w);
  float swell=sin(depth*0.68+time*1.65+sin(dot(p,vec2(0.06,0.04)))*0.38);
  return shore*(1.0-smoothstep(55.0,110.0,depth))*scale*(0.19*swell+0.07*sin(dot(p,vec2(0.33,0.21))-time*1.25)+0.035*sin(dot(p,vec2(-0.51,0.43))+time*1.85));
}
vec3 waveNormal(vec2 p,float depth,float time){
  vec2 seaward=vec2(0);float eps=0.12;
  // Distance increases in the axis normal to the coast.
  vec2 center=(g.waterBounds.xy+g.waterBounds.zw)*0.5;
  vec2 extent=g.waterBounds.zw-g.waterBounds.xy;
  if(extent.x>extent.y)seaward=vec2(0,sign(center.y));else seaward=vec2(sign(center.x),0);
  float h=waveHeight(p,depth,time);
  float dx=(waveHeight(p+vec2(eps,0),depth+seaward.x*eps,time)-h)/eps;
  float dz=(waveHeight(p+vec2(0,eps),depth+seaward.y*eps,time)-h)/eps;
  return normalize(vec3(-dx,1,-dz));
}

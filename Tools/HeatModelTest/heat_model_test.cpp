#include "Weapons/CannonHeatModel.h"
#include <cstdio>
using namespace ETM;
int main(){
  const float dt=0.001f; FHeatTuning T; int fails=0;
  // 1. time to overheat
  { FHeatState S; float t=0; int shots=0; while(!S.bOverheated){ shots+=StepHeat(S,T,dt,true,false); t+=dt; }
    printf("overheat at %.3fs after %d shots\n",t,shots); if(shots!=100||t<9.85f||t>9.95f) fails++; }
  // 2. recovery: venting must not help while locked (P-01)
  for(int v=0; v<2; ++v){ FHeatState S; S.Heat=100; S.bOverheated=true; S.TimeSinceLastShot=0; float t=0;
    while(S.bOverheated){ StepHeat(S,T,dt,false,v==1); t+=dt; }
    printf("recovery %s: %.3fs\n", v?"vent held":"passive", t); if(t<3.9f||t>4.1f) fails++; }
  // 3. sustained dps hold vs burst+vent over 60s
  auto dps=[&](int policy){ FHeatState S; bool venting=false; int shots=0;
    for(int i=0;i<60000;i++){ bool trig=true, vent=false;
      if(policy==1){ if(!venting && S.Heat>=T.HeatCapacity-2*T.HeatPerShot) venting=true; if(venting && S.Heat<=0) venting=false; trig=!venting; vent=venting; }
      shots+=StepHeat(S,T,dt,trig,vent);} return shots*25.0f/60.0f; };
  float h=dps(0), b=dps(1); printf("dps hold=%.1f burst+vent=%.1f\n",h,b);
  if(!(b>h*1.15f)) fails++;
  printf(fails? "FAILED %d\n":"ALL PASS\n", fails); return fails;
}

#include "../Source/VoxDSP.h"
#include <iostream>
int main(){
 for(double sr:{44100.,48000.,96000.})for(int n:{1,2}){
 VoxDSP d;d.prepare(sr*4);float v[]={1,1,1,1,.5f};
 for(int i=0;i<200000;++i){float s=.7f*std::sin(i*.03f),x[]={s,s};d.tick(x,n,1,v);for(int c=0;c<n;++c)if(!std::isfinite(x[c])||std::abs(x[c])>2)return 1;if(n==2&&x[0]!=x[1])return 2;}
 d.prepare(sr*4);float z[]={0,0,0,0,.5f};float x[]={.25f,-.25f};d.tick(x,n,0,z);if(std::abs(x[0]-.25f)>1e-6f)return 3;
 }
 std::cout<<"Finite output, stereo linking, neutral controls: OK\n";
}

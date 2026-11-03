#pragma once
#include <cmath>
#include <algorithm>
// Stereo-linked envelope compression; complementary one-pole tone bands.
// FET/Opto inspired timing, not circuit modelling. Runs at 4x host sample rate.
class VoxDSP {
 struct Channel {float rumble=0,body=0,high=0;} ch[2];
 float sr=192000,cr=0,cb=0,ct=0,fast=0,slow=0,ess=0;
 float af=0,rf=0,as=0,rs=0,ae=0,re=0;
 float follow(float x,float e,float a,float r){return x+(x>e?a:r)*(e-x);}
 float gain(float e,float threshold,float ratio){
  float over=20*std::log10(std::max(e,1e-8f))-threshold;
  // 6 dB soft knee.
  float reduction=over<=-3?0:over<3?(over+3)*(over+3)/12:over;
  return std::pow(10.f,-reduction*(1-1/ratio)/20);
 }
public:
 void prepare(double rate){sr=(float)rate;auto lp=[&](float f){return 1-std::exp(-6.2831853f*f/sr);};
 cr=lp(85);cb=lp(320);ct=lp(5500);
 auto t=[&](float s){return std::exp(-1/(s*sr));};
 af=t(.001);rf=t(.065);as=t(.018);rs=t(.24);ae=t(.0005);re=t(.055);
 ch[0]={};ch[1]={};fast=slow=ess=0;
 }
 void tick(float* x,int n,float clean,const float* v){
 float peak=0;
 for(int c=0;c<n;++c){auto& s=ch[c];s.rumble+=cr*(x[c]-s.rumble);s.body+=cb*(x[c]-s.body);x[c]-=clean*(s.rumble+.22f*(s.body-s.rumble));peak=std::max(peak,std::abs(x[c]));}
 fast=follow(peak,fast,af,rf);
 float gf=gain(fast,-8-16*v[0],4);
 slow=follow(peak*gf,slow,as,rs);
 float gc=gf*gain(slow,-14-12*v[0],2);
 gc=(1-v[0])+v[0]*gc*std::pow(10.f,5*v[0]/20);
 float hi[2]{},hp=0,full=0;
 for(int c=0;c<n;++c){x[c]*=gc;auto& s=ch[c];s.high+=ct*(x[c]-s.high);hi[c]=x[c]-s.high;hp=std::max(hp,std::abs(hi[c]));full=std::max(full,std::abs(x[c]));}
 float detect=std::clamp((hp/(full+.003f)-.3f)/.45f,0.f,1.f);
 ess=follow(detect,ess,ae,re);
 float out=std::pow(10.f,(v[4]-.5f)*24/20);
 for(int c=0;c<n;++c){float h=hi[c]*(1-.8f*v[1]*ess);float y=x[c]-hi[c]+h*(1+.65f*v[2]);float drive=1+2*v[3];y+=v[3]*.3f*(std::tanh(y*drive)/drive-y);x[c]=y*out;}
 }
};

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static void u32(FILE*f,uint32_t n){for(int i=0;i<4;i++)fputc(n>>(8*i),f);}
static void u16(FILE*f,int n){fputc(n,f);fputc(n>>8,f);}
int main(void){
 const int sr=22050; double pi=acos(-1);
 for(int song=0;song<2;song++){
 FILE*f=fopen(song?"pistas/02-lluvia-lenta.wav":"pistas/01-cafe-de-noche.wav","wb");if(!f)return 1;
 double beat=60.0/(song?72:78),dur=beat*128;int count=sr*dur;
 fwrite("RIFF",1,4,f);u32(f,36+count*2);fwrite("WAVEfmt ",1,8,f);u32(f,16);u16(f,1);u16(f,1);u32(f,sr);u32(f,sr*2);u16(f,2);u16(f,16);fwrite("data",1,4,f);u32(f,count*2);
 int roots[2][4]={{48,53,57,55},{50,55,48,53}},steps[]={0,3,7,10};unsigned seed=1234+song;
 for(int i=0;i<count;i++){
 double t=(double)i/sr,b=t/beat;int bar=(int)(b/4)%4;double q=fmod(b,1)*beat,s=0;
 for(int k=0;k<4;k++){double n=roots[song][bar]+steps[k]+12,hz=440*pow(2,(n-69)/12),age=fmod(t,beat*4);s+=.064*exp(-age*.85)*(sin(2*pi*hz*t)+.24*sin(2*pi*hz*2*t))*fmin(age*35,1);}
 double bass=440*pow(2,(roots[song][bar]-12-69)/12.0);s+=.16*sin(2*pi*bass*t)*exp(-q*5)*fmin(q*100,1);
 double kick=fmod(b,2)*beat;s+=.26*sin(2*pi*(48*kick+7*(1-exp(-kick*25))))*exp(-kick*16);
 seed=1664525*seed+1013904223;double noise=((seed>>8)/16777216.0)*2-1;
 double sn=fmod(b+1,2)*beat;s+=noise*.12*exp(-sn*35);double hat=fmod(b,.5)*beat;s+=noise*.042*exp(-hat*95);
 int notes[]={12,7,10,3,14,10,7,3};double ma=fmod(b,.5)*beat;double mhz=440*pow(2,(roots[song][bar]+24+notes[((int)(b*2)/2)%8]-69)/12.0);s+=.045*sin(2*pi*mhz*t)*exp(-ma*9)*fmin(ma*90,1);
 s+=noise*.0025;s*=fmin(t/2,1)*fmin((dur-t)/3,1);u16(f,(int)(tanh(s)*25000));
 }fclose(f);
 }return 0;
}

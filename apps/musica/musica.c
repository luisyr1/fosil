#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static SDL_Renderer*r;static TTF_Font*font,*big;static SDL_AudioDeviceID dev;static SDL_AudioSpec spec;
static Uint8*pcm;static Uint32 len,pos;static int playing=0,track=0,volume=45;static char error[180];
static const char*titles[]={"Café de noche","Lluvia lenta"};
static const char*files[]={"pistas/01-cafe-de-noche.wav","pistas/02-lluvia-lenta.wav"};
static void audio(void*u,Uint8*out,int n){(void)u;memset(out,0,n);if(!playing||!pcm)return;Uint32 left=len-pos;int take=left<(Uint32)n?(int)left:n;SDL_MixAudioFormat(out,pcm+pos,spec.format,take,volume*SDL_MIX_MAXVOLUME/100);pos+=take;if(pos>=len)playing=0;}
static int load(int idx){if(dev)SDL_CloseAudioDevice(dev);dev=0;if(pcm)SDL_FreeWAV(pcm);pcm=NULL;pos=0;track=idx;playing=0;error[0]=0;if(!SDL_LoadWAV(files[idx],&spec,&pcm,&len)){snprintf(error,sizeof error,"No se pudo leer la pista");return 0;}
/* ALSA de este móvil no abre 22050 Hz desde SDL: se convierte a 48 kHz estéreo al cargar */
SDL_AudioCVT cvt;if(SDL_BuildAudioCVT(&cvt,spec.format,spec.channels,spec.freq,AUDIO_S16SYS,2,48000)>0){cvt.len=len;cvt.buf=SDL_malloc((size_t)len*cvt.len_mult);if(cvt.buf){memcpy(cvt.buf,pcm,len);SDL_ConvertAudio(&cvt);SDL_FreeWAV(pcm);pcm=cvt.buf;len=cvt.len_cvt;spec.format=AUDIO_S16SYS;spec.channels=2;spec.freq=48000;}}spec.callback=audio;dev=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);if(!dev){snprintf(error,sizeof error,"Audio no disponible en este sistema");fprintf(stderr,"Audio: %s\n",SDL_GetError());return 0;}SDL_PauseAudioDevice(dev,0);return 1;}
static void box(int x,int y,int w,int h,int cr,int cg,int cb){SDL_SetRenderDrawColor(r,cr,cg,cb,255);SDL_Rect a={x,y,w,h};SDL_RenderFillRect(r,&a);}
static void text(const char*s,int x,int y,int large,int c){SDL_Color col={(c>>16)&255,(c>>8)&255,c&255,255};SDL_Surface*a=TTF_RenderUTF8_Blended(large?big:font,s,col);if(!a)return;SDL_Texture*t=SDL_CreateTextureFromSurface(r,a);SDL_Rect d={x,y,a->w,a->h};SDL_RenderCopy(r,t,NULL,&d);SDL_DestroyTexture(t);SDL_FreeSurface(a);}
static void tap(int x,int y){if(y<90&&x>390){SDL_Event e={.type=SDL_QUIT};SDL_PushEvent(&e);return;}if(y>490&&y<575){if(x<170){load((track+1)%2);if(dev)playing=1;}else if(x>370){load((track+1)%2);if(dev)playing=1;}else if(dev){SDL_LockAudioDevice(dev);if(pos>=len)pos=0;playing=!playing;SDL_UnlockAudioDevice(dev);}else load(track);}
if(y>420&&y<475&&dev){SDL_LockAudioDevice(dev);double f=(x-35)/470.0;if(f<0)f=0;if(f>1)f=1;pos=(Uint32)(len*f);pos-=pos%4;SDL_UnlockAudioDevice(dev);}
if(y>610&&y<665){if(x<270)volume=volume>5?volume-5:0;else volume=volume<95?volume+5:100;}
if(y>710&&y<790){load(0);if(dev)playing=1;}if(y>=790&&y<875){load(1);if(dev)playing=1;}}
int main(int argc,char**argv){int test=argc>1&&!strcmp(argv[1],"--test");
if(SDL_Init(SDL_INIT_AUDIO|(test?0:SDL_INIT_VIDEO|SDL_INIT_EVENTS))){fprintf(stderr,"%s\n",SDL_GetError());return 1;}
if(test){for(int i=0;i<2;i++){if(!load(i))return 2;printf("%s: %.1f s, %d Hz\n",titles[i],(double)len/(spec.freq*4),spec.freq);playing=1;SDL_Delay(600);if(!pos)return 3;}SDL_CloseAudioDevice(dev);SDL_FreeWAV(pcm);SDL_Quit();return 0;}
TTF_Init();font=TTF_OpenFont("/usr/share/fonts/opensans/OpenSans-Regular.ttf",22);big=TTF_OpenFont("/usr/share/fonts/opensans/OpenSans-Light.ttf",36);if(!font||!big)return 1;
SDL_Window*w=SDL_CreateWindow("Música",0,0,540,960,SDL_WINDOW_FULLSCREEN);if(!w){fprintf(stderr,"%s\n",SDL_GetError());return 1;}r=SDL_CreateRenderer(w,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);if(!r)r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);if(!r)return 1;SDL_RenderSetLogicalSize(r,540,960);SDL_ShowCursor(SDL_DISABLE);load(0);int done=0;Uint32 start=SDL_GetTicks();
while(!done){SDL_Event e;while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT)done=1;if(e.type==SDL_KEYDOWN&&(e.key.keysym.sym==SDLK_ESCAPE||e.key.keysym.sym==SDLK_VOLUMEDOWN))done=1;if(e.type==SDL_FINGERUP)tap(e.tfinger.x*540,e.tfinger.y*960);if(e.type==SDL_MOUSEBUTTONUP&&e.button.which!=SDL_TOUCH_MOUSEID){int ww,hh;SDL_GetWindowSize(w,&ww,&hh);tap(e.button.x*540/ww,e.button.y*960/hh);}}
if(dev&&pos>=len&&len){load((track+1)%2);if(dev)playing=1;}
box(0,0,540,960,16,23,29);text("FÓSIL / MÚSICA",30,28,0,0x9daeb6);text("Salir",435,28,0,0xd6ece4);
box(35,105,470,225,26,43,47);for(int i=0;i<35;i++){double phase=(SDL_GetTicks()-start)/600.0;int h=20+45*(1+sin(i*.65+(playing?phase:0)));box(62+i*12,217-h/2,5,h,83+i*2,151,133);}text("LOFI · SESIONES",60,280,0,0xbad1c6);
text(titles[track],35,350,1,0xe5efe9);text(track?"72 BPM · suave y tranquilo":"78 BPM · acordes de madrugada",35,399,0,0x9daeb6);
box(35,449,470,5,51,66,71);if(len)box(35,449,(int)(470.0*pos/len),5,147,205,174);
char line[80];int seconds=pos/(spec.freq?spec.freq*4:1),total=len/(spec.freq?spec.freq*4:1);snprintf(line,sizeof line,"%d:%02d / %d:%02d",seconds/60,seconds%60,total/60,total%60);text(line,35,465,0,0x9daeb6);
box(35,510,120,64,31,48,51);box(175,510,190,64,50,84,74);box(385,510,120,64,31,48,51);text("Anterior",47,527,0,0xe5efe9);text(playing?"Pausa":"Reproducir",192,527,0,0xe5efe9);text("Siguiente",394,527,0,0xe5efe9);
snprintf(line,sizeof line,"−       Volumen %d%%       +",volume);text(line,85,620,0,0xbad1c6);
text("DOS TEMAS ORIGINALES · SIN CONEXIÓN",35,685,0,0x9daeb6);for(int i=0;i<2;i++){box(35,727+i*80,470,65,track==i?35:23,track==i?61:35,track==i?54:40);text(titles[i],55,743+i*80,0,0xe5efe9);}
text(error[0]?error:"Toca una pista para escucharla",35,900,0,error[0]?0xe5b389:0x9daeb6);if(getenv("MUSICA_CAPTURA")){SDL_Surface*shot=SDL_CreateRGBSurfaceWithFormat(0,540,960,32,SDL_PIXELFORMAT_ARGB8888);if(shot){SDL_RenderReadPixels(r,NULL,shot->format->format,shot->pixels,shot->pitch);SDL_SaveBMP(shot,getenv("MUSICA_CAPTURA"));SDL_FreeSurface(shot);}done=1;}SDL_RenderPresent(r);SDL_Delay(40);}
if(dev){SDL_CloseAudioDevice(dev);}
if(pcm){SDL_FreeWAV(pcm);}
TTF_CloseFont(font);TTF_CloseFont(big);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);TTF_Quit();SDL_Quit();return 0;}

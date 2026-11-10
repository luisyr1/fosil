from pathlib import Path
import subprocess as sp,shutil,re,sys,os
# Uso: preparar.py <directorio de salida>  (por defecto /var/lib/fosil/audio)
base=Path(sys.argv[1] if len(sys.argv)>1 else '/var/lib/fosil/audio');base.mkdir(parents=True,exist_ok=True)
boot=Path(os.environ.get('FOSIL_BOOT','/boot'))
original=base/'original.dtb'
if not original.exists():shutil.copy2(boot/'msm8916-motorola-surnia.dtb',original)
out=base/'surnia-audio.dtb';shutil.copy2(original,out)
def call(*args):return sp.check_output(args,text=True).strip()
def put(node,prop,*values,typ='s'):sp.run(['fdtput','-t',typ,str(out),node,prop,*map(str,values)],check=True)
def add(node):sp.run(['fdtput','-p','-c',str(out),node],check=True)
def delete(node):sp.run(['fdtput','-r',str(out),node],check=True)
lpass='/soc@0/audio-controller@7708000';sound='/soc@0/sound@7702000'
src=call('dtc','-q','-I','dtb','-O','dts',str(out))
phandle=max(int(x,16) for x in re.findall(r'phandle = <0x([0-9a-f]+)>',src))+1
put(lpass,'phandle',hex(phandle),typ='x');put(lpass,'status','okay')
put('/soc@0/remoteproc@4080000','status','disabled')
put(sound,'compatible','qcom,apq8016-sbc-sndcard')
for node in call('fdtget','-l',str(out),sound).splitlines():delete(sound+'/'+node)
for name,dai,codec in [('primary',0,0),('tertiary',2,1)]:
 node=sound+'/'+name+'-dai-link';add(node+'/cpu');add(node+'/codec')
 put(node,'link-name','WCD' if dai==0 else 'WCD-Capture')
 put(node+'/cpu','sound-dai',hex(phandle),hex(dai),typ='x')
 # Preserve the existing codec phandles, obtained from original backend links.
 original_link='/soc@0/sound@7702000/backend'+str(codec)+'-dai-link/codec'
 cells=call('fdtget','-t','x',str(original),original_link,'sound-dai').split()
 put(node+'/codec','sound-dai',*cells,typ='x')
sp.run(['dtc','-q','-I','dtb','-O','dts',str(out),'-o',str(base/'surnia-audio.dts')],check=True)
old=(boot/'extlinux/extlinux.conf').read_text()
if not (base/'extlinux.original.conf').exists():(base/'extlinux.original.conf').write_text(old)
old=(base/'extlinux.original.conf').read_text()
entry=old[old.index('label postmarketOS'):].replace('label postmarketOS','label audio-directo',1).replace('/msm8916-motorola-surnia.dtb','/msm8916-surnia-audio.dtb')
new=old.replace('default postmarketOS','default audio-directo').replace('timeout 1','timeout 5')+'\n'+entry
(base/'extlinux.audio.conf').write_text(new)
assert call('fdtget',str(out),lpass,'status')=='okay'
assert call('fdtget',str(out),sound,'compatible')=='qcom,apq8016-sbc-sndcard'
assert call('fdtget',str(out),'/soc@0/remoteproc@4080000','status')=='disabled'
print('DTB validado; módem desactivado; dos enlaces directos; configuración original conservada.')

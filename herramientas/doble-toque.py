import struct,time,glob,fcntl,array
def ev(t,c,v): return struct.pack('qqHHi',0,0,t,c,v)
fd=None
for p in glob.glob('/sys/class/input/event*/device/name'):
    if 'Synaptics' in open(p).read():
        fd=open('/dev/input/'+p.split('/')[4],'wb',buffering=0)
def tap(i):
    fd.write(ev(3,0x2f,0)+ev(3,0x39,i)+ev(3,0x35,500)+ev(3,0x36,900)+ev(1,0x14a,1)+ev(0,0,0))
    time.sleep(.06)
    fd.write(ev(3,0x39,-1)+ev(1,0x14a,0)+ev(0,0,0))
tap(80); time.sleep(.12); tap(81)

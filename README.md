# smm-rw
uefi smm rw; reads/writes memory in ring -2, controlled by usermode client, supports intel/amd cpus.


communication through shared memory by default (can be made through nvram). smm inits on first smi after os boot, finds ntoskrnl, system cr3, etc.

if u want to use it, control smi count under 100 per second (100 is already ALOT and will be detected for sure, also may cause cpu lags; less = better), more smi = more cpu lag(smm takes cpu on each smi until smm done). so, that's the main detection vector, if u do many smi per second more time your cpu stays in smm and that can be measured for example by rdtsc/rdtscp timing.

## disclaimer

research only

## structure

```
smm/ - uefi smm driver
um/  - usermode client (windows x64)
```

## build

need vs2022 + windows sdk + edk2(https://github.com/tianocore/edk2).

```
cd smm && build.bat for smm build ( .efi )
cd um  && build.bat for client build ( client.exe )
```
## flashing
so, to flash you need your motherboard's firmware ( you can find it on vendor website ) and after using uefitool 0.26.0(i used) open your BIOS firmware file and replace the body of some useless smm module. for example some PowerLed smm module which is what i used. module guids are different on each firmware, so you need to find one for yourself.

then save bios firmware file and flash your bios

bios flashing can be done using some MFlash(MSI) or EZFlash(ASUS) utilities in your bios, but not on every motherboard, some motherboards that have bios flashback block unsigned firmware from being installed using their utilities, so if your motherboard has bios flashback you need to use it to install unsigned firmware, flashback usually doesn't check if your firmware is signed or not, you can also find guide how to flash bios in internet depending on your vendor.  

## commands

- ping
- read/write virtual memory
- read/write physical memory
- find process by name
- get cr3
- get module base
- setup shared memory

## how it works

1. smm module is in SMRAM, os doesn't have access to its memory
2. client writes command to NVRAM variable
3. any SMI triggers the handler, it processes the command
4. after handshake switches to shared memory, personally i used nvram variables after only to generate smi(trigger smm), i used delete kicks to not generate lags. P.S ALL nvram writes/delete will trigger smi -> smm handler will be triggered
5. profit
<img width="500" height="115" alt="image" src="https://github.com/user-attachments/assets/f32c19c4-fc03-4ebc-8a2c-9df21063d760" />



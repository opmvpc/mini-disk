@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
cl /nologo /O2 /GS- /Gs9999999 /GR- /EHa- /Oi /W4 /c min.c
link /nologo /NODEFAULTLIB /ENTRY:entry_point /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MERGE:.rdata=.text /ALIGN:16 /FILEALIGN:16 /NOCOFFGRPINFO /EMITPOGOPHASEINFO /STACK:0x100000,0x100000 /OUT:min.exe min.obj kernel32.lib user32.lib

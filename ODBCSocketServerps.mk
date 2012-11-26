
SQLSocketServerps.dll: dlldata.obj SQLSocketServer_p.obj SQLSocketServer_i.obj
	link /dll /out:SQLSocketServerps.dll /def:SQLSocketServerps.def /entry:DllMain dlldata.obj SQLSocketServer_p.obj SQLSocketServer_i.obj \
		kernel32.lib rpcndr.lib rpcns4.lib rpcrt4.lib oleaut32.lib uuid.lib \

.c.obj:
	cl /c /Ox /DWIN32 /D_WIN32_WINNT=0x0400 /DREGISTER_PROXY_DLL \
		$<

clean:
	@del SQLSocketServerps.dll
	@del SQLSocketServerps.lib
	@del SQLSocketServerps.exp
	@del dlldata.obj
	@del SQLSocketServer_p.obj
	@del SQLSocketServer_i.obj

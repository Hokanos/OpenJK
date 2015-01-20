#include <csignal>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include "qcommon/qcommon.h"
#include "sys_local.h"
#include "sys_loadlib.h"
#include "sys_public.h"
#include "con_local.h"

static char binaryPath[ MAX_OSPATH ] = { 0 };
static char installPath[ MAX_OSPATH ] = { 0 };

cvar_t *com_minimized;
cvar_t *com_unfocused;

/*
=================
Sys_SetBinaryPath
=================
*/
void Sys_SetBinaryPath(const char *path)
{
	Q_strncpyz(binaryPath, path, sizeof(binaryPath));
}

/*
=================
Sys_BinaryPath
=================
*/
char *Sys_BinaryPath(void)
{
	return binaryPath;
}

/*
=================
Sys_SetDefaultInstallPath
=================
*/
void Sys_SetDefaultInstallPath(const char *path)
{
	Q_strncpyz(installPath, path, sizeof(installPath));
}

/*
=================
Sys_DefaultInstallPath
=================
*/
char *Sys_DefaultInstallPath(void)
{
	if (*installPath)
		return installPath;
	else
		return Sys_Cwd();
}

/*
=================
Sys_DefaultAppPath
=================
*/
char *Sys_DefaultAppPath(void)
{
	return Sys_BinaryPath();
}

/*
==================
Sys_GetClipboardData
==================
*/
char *Sys_GetClipboardData( void ) {
#ifdef DEDICATED
	return NULL;
#else
	if ( !SDL_HasClipboardText() )
		return NULL;

	char *cbText = SDL_GetClipboardText();
	size_t len = strlen( cbText ) + 1;

	char *buf = (char *)Z_Malloc( len, TAG_CLIPBOARD );
	Q_strncpyz( buf, cbText, len );

	SDL_free( cbText );
	return buf;
#endif
}

/*
=================
Sys_ConsoleInput

Handle new console input
=================
*/
char *Sys_ConsoleInput(void)
{
	return CON_Input( );
}

void Sys_Print( const char *msg ) {
	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if ( !Q_strncmp( msg, "[skipnotify]", 12 ) ) {
		msg += 12;
	}
	if ( msg[0] == '*' ) {
		msg += 1;
	}
	ConsoleLogAppend( msg );
	CON_Print( msg );
}

/*
================
Sys_Init

Called after the common systems (cvars, files, etc)
are initialized
================
*/
void Sys_Init( void ) {
	Cmd_AddCommand ("in_restart", IN_Restart);
	Cvar_Get( "arch", OS_STRING " " ARCH_STRING, CVAR_ROM );
	Cvar_Get( "username", Sys_GetCurrentUser(), CVAR_ROM );
	com_unfocused = Cvar_Get( "com_unfocused", "0", CVAR_ROM );
	com_minimized = Cvar_Get( "com_minimized", "0", CVAR_ROM );
}

static void __attribute__((noreturn)) Sys_Exit( int ex ) {
	IN_Shutdown( );
#ifndef DEDICATED
	SDL_Quit( );
#endif

	NET_Shutdown( );

	Sys_PlatformExit( );

	Com_ShutdownHunkMemory( );
	Com_ShutdownZoneMemory( );

	CON_Shutdown();

    exit(ex);
}

#if !defined(DEDICATED)
static void Sys_ErrorDialog( const char *error )
{
	time_t rawtime;
	char timeStr[32] = {}; // should really only reach ~19 chars
	char crashLogPath[64];

	time( &rawtime );
	strftime( timeStr, sizeof( timeStr ), "%Y-%m-%d_%H-%M-%S", localtime( &rawtime ) ); // or gmtime
	Com_sprintf( crashLogPath, sizeof( crashLogPath ), "crashlog-%s.txt", timeStr );

	const char *errorMessage = va( "%s\nThe crash log was written to %s", error, crashLogPath );
	if ( SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Error", errorMessage, NULL ) < 0 )
	{
		fprintf( stderr, "%s", errorMessage );
	}

	FILE *fp = fopen( crashLogPath, "w" );
	if ( fp )
	{
		ConsoleLogWriteOut( fp );
		fclose( fp );
	}
	else
	{
		// Getting pretty desperate now
		fprintf( stderr, "Failed to open crash log. Writing out to console...\n" );
		ConsoleLogWriteOut( stderr );
		fflush( stderr );
	}
}
#endif

void Sys_Error( const char *error, ... )
{
	va_list argptr;
	char    string[1024];

	va_start (argptr,error);
	Q_vsnprintf (string, sizeof(string), error, argptr);
	va_end (argptr);

	Sys_Print( string );
#if !defined(DEDICATED)
	Sys_ErrorDialog( string );
#endif

	Sys_Exit( 3 );
}

void Sys_Quit (void) {
    Sys_Exit(0);
}

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int Sys_FileTime( char *path )
{
	struct stat buf;

	if ( stat( path, &buf ) == -1 )
		return -1;

	return buf.st_mtime;
}

/*
=================
Sys_UnloadDll
=================
*/
void Sys_UnloadDll( void *dllHandle )
{
	if( !dllHandle )
	{
		Com_Printf("Sys_UnloadDll(NULL)\n");
		return;
	}

	Sys_UnloadLibrary(dllHandle);
}

/*
=================
Sys_LoadDll

First try to load library name from system library path,
from executable path, then fs_basepath.
=================
*/
void *Sys_LoadDll(const char *name, qboolean useSystemLib)
{
	void *dllhandle = NULL;

	if(useSystemLib)
		Com_Printf("Trying to load \"%s\"...\n", name);

	if(!useSystemLib || !(dllhandle = Sys_LoadLibrary(name)))
	{
		char libPath[MAX_OSPATH];
		const char *topDir = Sys_BinaryPath();

		if(!*topDir)
			topDir = ".";

		Com_Printf("Trying to load \"%s\" from \"%s\"...\n", name, topDir);
		Com_sprintf(libPath, sizeof(libPath), "%s%c%s", topDir, PATH_SEP, name);

		if(!(dllhandle = Sys_LoadLibrary(libPath)))
		{
			const char *basePath = Cvar_VariableString("fs_basepath");

			if(!basePath || !*basePath)
				basePath = ".";

			if(FS_FilenameCompare(topDir, basePath))
			{
				Com_Printf("Trying to load \"%s\" from \"%s\"...\n", name, basePath);
				Com_sprintf(libPath, sizeof(libPath), "%s%c%s", basePath, PATH_SEP, name);
				dllhandle = Sys_LoadLibrary(libPath);
			}

			if(!dllhandle)
			{
				Com_Printf("Loading \"%s\" failed\n", name);
			}
		}
	}

	return dllhandle;
}

#if defined(MACOS_X) && !defined(_JK2EXE)
void *Sys_LoadMachOBundle( const char *name )
{
	if ( !FS_LoadMachOBundle(name) )
		return NULL;

	char *homepath = Cvar_VariableString( "fs_homepath" );
	char *gamedir = Cvar_VariableString( "fs_game" );
	char dllName[MAX_QPATH];

	Com_sprintf( dllName, sizeof(dllName), "%s_pk3" DLL_EXT, name );

	//load the unzipped library
	char *fn = FS_BuildOSPath( homepath, gamedir, dllName );

	void    *libHandle = Sys_LoadLibrary( fn );

	if ( libHandle != NULL ) {
		Com_Printf( "Loaded pk3 bundle %s.\n", name );
	}

	return libHandle;
}
#endif

enum SearchPathFlag
{
	SEARCH_PATH_MOD		= 1 << 0,
	SEARCH_PATH_BASE	= 1 << 1,
	SEARCH_PATH_ROOT	= 1 << 2
};

static void *Sys_LoadDllFromPaths( const char *filename, const char *gamedir, const char **searchPaths,
									int numPaths, uint32_t searchFlags, const char *callerName )
{
	char *fn;
	void *libHandle;

	if ( searchFlags & SEARCH_PATH_MOD )
	{
		for ( int i = 0; i < numPaths; i++ )
		{
			const char *libDir = searchPaths[i];
			if ( !libDir[0] )
				continue;

			fn = FS_BuildOSPath( libDir, gamedir, filename );
			libHandle = Sys_LoadLibrary( fn );
			if ( libHandle )
				return libHandle;

			Com_Printf( "%s(%s) failed: \"%s\"\n", callerName, fn, Sys_LibraryError() );
		}
	}

	if ( searchFlags & SEARCH_PATH_BASE )
	{
		for ( int i = 0; i < numPaths; i++ )
		{
			const char *libDir = searchPaths[i];
			if ( !libDir[0] )
				continue;

			fn = FS_BuildOSPath( libDir, BASEGAME, filename );
			libHandle = Sys_LoadLibrary( fn );
			if ( libHandle )
				break;

			Com_Printf( "%s(%s) failed: \"%s\"\n", callerName, fn, Sys_LibraryError() );
		}
	}

	if ( searchFlags & SEARCH_PATH_ROOT )
	{
		for ( int i = 0; i < numPaths; i++ )
		{
			const char *libDir = searchPaths[i];
			if ( !libDir[0] )
				continue;

			fn = va( "%s/%s", libDir, filename );
			libHandle = Sys_LoadLibrary( fn );
			if ( libHandle )
				break;

			Com_Printf( "%s(%s) failed: \"%s\"\n", callerName, fn, Sys_LibraryError() );
		}
	}
		
	return NULL;
}

void *Sys_LoadLegacyGameDll( const char *name, VMMainProc **vmMain, SystemCallProc *systemcalls )
{
	void	*libHandle = NULL;
	char	filename[MAX_OSPATH];

	Com_sprintf (filename, sizeof(filename), "%s" ARCH_STRING DLL_EXT, name);

	if (!Sys_UnpackDLL(filename))
	{
		Com_DPrintf( "Sys_LoadLegacyGameDll: Failed to unpack %s from PK3.\n", filename );
		return NULL;
	}

#if defined(MACOS_X) && !defined(_JK2EXE)
    //First, look for the old-style mac .bundle that's inside a pk3
    //It's actually zipped, and the zipfile has the same name as 'name'
    libHandle = Sys_LoadMachOBundle( name );
#endif

	if (!libHandle) {
		//Com_Printf( "Sys_LoadGameDll(%s) failed: \"%s\"\n", filename, Sys_LibraryError() );

		char *basepath = Cvar_VariableString( "fs_basepath" );
		char *homepath = Cvar_VariableString( "fs_homepath" );
		char *cdpath = Cvar_VariableString( "fs_cdpath" );
		char *gamedir = Cvar_VariableString( "fs_game" );
#ifdef MACOS_X
        char *apppath = Cvar_VariableString( "fs_apppath" );
#endif

		const char *searchPaths[] = {
			basepath,
			homepath,
#ifdef MACOS_X
			apppath,
#endif
			cdpath,
		};
		int numPaths = ARRAY_LEN( searchPaths );

		libHandle = Sys_LoadDllFromPaths( filename, gamedir, searchPaths, numPaths, SEARCH_PATH_BASE | SEARCH_PATH_MOD, __FUNCTION__ );
		if ( !libHandle )
			return NULL;
	}

	typedef void QDECL DllEntryProc( SystemCallProc *syscallptr );

	DllEntryProc *dllEntry = (DllEntryProc *)Sys_LoadFunction( libHandle, "dllEntry" );
	*vmMain = (VMMainProc *)Sys_LoadFunction( libHandle, "vmMain" );

	if ( !*vmMain || !dllEntry ) {
		Com_DPrintf ( "Sys_LoadGameDll(%s) failed to find vmMain function:\n...%s!\n", name, Sys_LibraryError() );
		Sys_UnloadLibrary( libHandle );
		return NULL;
	}

	Com_DPrintf ( "Sys_LoadGameDll(%s) found vmMain function at 0x%" PRIxPTR "\n", name, *vmMain );
	dllEntry( systemcalls );

	return libHandle;
}

void *Sys_LoadSPGameDll( const char *name, GetGameAPIProc **GetGameAPI )
{
	void	*libHandle = NULL;
	char	filename[MAX_OSPATH];

	assert( GetGameAPI );

	Com_sprintf (filename, sizeof(filename), "%s" ARCH_STRING DLL_EXT, name);

#if defined(MACOS_X) && !defined(_JK2EXE)
    //First, look for the old-style mac .bundle that's inside a pk3
    //It's actually zipped, and the zipfile has the same name as 'name'
    libHandle = Sys_LoadMachOBundle( name );
#endif

	if (!libHandle) {
		char *basepath = Cvar_VariableString( "fs_basepath" );
		char *homepath = Cvar_VariableString( "fs_homepath" );
		char *cdpath = Cvar_VariableString( "fs_cdpath" );
		char *gamedir = Cvar_VariableString( "fs_game" );
#ifdef MACOS_X
        char *apppath = Cvar_VariableString( "fs_apppath" );
#endif

		const char *searchPaths[] = {
			basepath,
			homepath,
#ifdef MACOS_X
			apppath,
#endif
			cdpath,
		};
		int numPaths = ARRAY_LEN( searchPaths );

		libHandle = Sys_LoadDllFromPaths( filename, gamedir, searchPaths, numPaths,
											SEARCH_PATH_BASE | SEARCH_PATH_MOD | SEARCH_PATH_ROOT,
											__FUNCTION__ );
		if ( !libHandle )
			return NULL;
	}

	*GetGameAPI = (GetGameAPIProc *)Sys_LoadFunction( libHandle, "GetGameAPI" );
	if ( !*GetGameAPI ) {
		Com_DPrintf ( "%s(%s) failed to find GetGameAPI function:\n...%s!\n", __FUNCTION__, name, Sys_LibraryError() );
		Sys_UnloadLibrary( libHandle );
		return NULL;
	}

	return libHandle;
}

void *Sys_LoadGameDll( const char *name, GetModuleAPIProc **moduleAPI )
{
	void	*libHandle = NULL;
	char	filename[MAX_OSPATH];

	Com_sprintf (filename, sizeof(filename), "%s" ARCH_STRING DLL_EXT, name);

	if (!Sys_UnpackDLL(filename))
	{
		Com_DPrintf( "Sys_LoadGameDll: Failed to unpack %s from PK3.\n", filename );
		return NULL;
	}

#if defined(MACOS_X) && !defined(_JK2EXE)
    //First, look for the old-style mac .bundle that's inside a pk3
    //It's actually zipped, and the zipfile has the same name as 'name'
    libHandle = Sys_LoadMachOBundle( name );
#endif

	if (!libHandle) {
		//Com_Printf( "Sys_LoadGameDll(%s) failed: \"%s\"\n", filename, Sys_LibraryError() );

		char *basepath = Cvar_VariableString( "fs_basepath" );
		char *homepath = Cvar_VariableString( "fs_homepath" );
		char *cdpath = Cvar_VariableString( "fs_cdpath" );
		char *gamedir = Cvar_VariableString( "fs_game" );
#ifdef MACOS_X
        char *apppath = Cvar_VariableString( "fs_apppath" );
#endif

		const char *searchPaths[] = {
			basepath,
			homepath,
#ifdef MACOS_X
			apppath,
#endif
			cdpath,
		};
		int numPaths = ARRAY_LEN( searchPaths );

		libHandle = Sys_LoadDllFromPaths( filename, gamedir, searchPaths, numPaths, SEARCH_PATH_BASE | SEARCH_PATH_MOD, __FUNCTION__ );
		if ( !libHandle )
			return NULL;
	}

	*moduleAPI = (GetModuleAPIProc *)Sys_LoadFunction( libHandle, "GetModuleAPI" );
	if ( !*moduleAPI ) {
		Com_DPrintf ( "Sys_LoadGameDll(%s) failed to find GetModuleAPI function:\n...%s!\n", name, Sys_LibraryError() );
		Sys_UnloadLibrary( libHandle );
		return NULL;
	}

	return libHandle;
}

/*
=================
Sys_SigHandler
=================
*/
void Sys_SigHandler( int signal )
{
	static qboolean signalcaught = qfalse;

	if( signalcaught )
	{
		fprintf( stderr, "DOUBLE SIGNAL FAULT: Received signal %d, exiting...\n",
			signal );
	}
	else
	{
		signalcaught = qtrue;
		//VM_Forced_Unload_Start();
#ifndef DEDICATED
		CL_Shutdown();
		//CL_Shutdown(va("Received signal %d", signal), qtrue, qtrue);
#endif
		SV_Shutdown(va("Received signal %d", signal) );
		//VM_Forced_Unload_Done();
	}

	if( signal == SIGTERM || signal == SIGINT )
		Sys_Exit( 1 );
	else
		Sys_Exit( 2 );
}

#ifdef MACOS_X
/*
 =================
 Sys_StripAppBundle

 Discovers if passed dir is suffixed with the directory structure of a Mac OS X
 .app bundle. If it is, the .app directory structure is stripped off the end and
 the result is returned. If not, dir is returned untouched.
 =================
 */
char *Sys_StripAppBundle( char *dir )
{
	static char cwd[MAX_OSPATH];

	Q_strncpyz(cwd, dir, sizeof(cwd));
	if(strcmp(Sys_Basename(cwd), "MacOS"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	if(strcmp(Sys_Basename(cwd), "Contents"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	if(!strstr(Sys_Basename(cwd), ".app"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	return cwd;
}
#endif

#ifndef DEFAULT_BASEDIR
#	ifdef MACOS_X
#		define DEFAULT_BASEDIR Sys_StripAppBundle(Sys_BinaryPath())
#	else
#		define DEFAULT_BASEDIR Sys_BinaryPath()
#	endif
#endif

int main ( int argc, char* argv[] )
{
	int		i;
	char	commandLine[ MAX_STRING_CHARS ] = { 0 };

	CON_Init();
	Sys_PlatformInit();

	// get the initial time base
	Sys_Milliseconds();

#ifdef MACOS_X
	// This is passed if we are launched by double-clicking
	if ( argc >= 2 && Q_strncmp ( argv[1], "-psn", 4 ) == 0 )
		argc = 1;
#endif

	Sys_SetBinaryPath( Sys_Dirname( argv[ 0 ] ) );
	Sys_SetDefaultInstallPath( DEFAULT_BASEDIR );

	// Concatenate the command line for passing to Com_Init
	for( i = 1; i < argc; i++ )
	{
		const bool containsSpaces = (strchr(argv[i], ' ') != NULL);
		if (containsSpaces)
			Q_strcat( commandLine, sizeof( commandLine ), "\"" );

		Q_strcat( commandLine, sizeof( commandLine ), argv[ i ] );

		if (containsSpaces)
			Q_strcat( commandLine, sizeof( commandLine ), "\"" );

		Q_strcat( commandLine, sizeof( commandLine ), " " );
	}

	Com_Init (commandLine);

	NET_Init();

	// main game loop
	while (1)
	{
		bool shouldSleep = false;

#if !defined(_JK2EXE)
		if ( com_dedicated->integer )
		{
			shouldSleep = true;
		}
#endif

#if !defined(DEDICATED)
		if ( com_minimized->integer )
		{
			shouldSleep = true;
		}
#endif

		if ( shouldSleep )
		{
			Sys_Sleep( 5 );
		}

		// make sure mouse and joystick are only called once a frame
		IN_Frame();

		// run the game
		Com_Frame();
	}

	// never gets here
	return 0;
}
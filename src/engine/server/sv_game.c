/*
===========================================================================

Daemon GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of the Daemon GPL Source Code (Daemon Source Code).

Daemon Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Daemon Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Daemon Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following the
terms and conditions of the GNU General Public License which accompanied the Daemon
Source Code.  If not, please request a copy in writing from id Software at the address
below.

If you have questions concerning this license or the applicable additional terms, you
may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville,
Maryland 20850 USA.

===========================================================================
*/

// sv_game.c -- interface to the game module

#include "server.h"
#include "../qcommon/crypto.h"

void            CMod_PhysicsAddEntity( sharedEntity_t *gEnt );
void            CMod_PhysicsAddStatic( const sharedEntity_t *gEnt );

// these functions must be used instead of pointer arithmetic, because
// the game allocates gentities with private information after the server shared part
int SV_NumForGentity( sharedEntity_t *ent )
{
	int num;

	num = ( ( byte * ) ent - ( byte * ) sv.gentities ) / sv.gentitySize;

	return num;
}

sharedEntity_t *SV_GentityNum( int num )
{
	sharedEntity_t *ent;

	if ( num >= MAX_GENTITIES )
	{
		Com_Error( ERR_DROP, "SV_GentityNum: bad num" );
	}

	ent = ( sharedEntity_t * )( ( byte * ) sv.gentities + sv.gentitySize * ( num ) );

	return ent;
}

playerState_t  *SV_GameClientNum( int num )
{
	playerState_t *ps;

	if ( num >= sv_maxclients->integer )
	{
		Com_Error( ERR_DROP, "SV_GameClientNum: bad num" );
	}
	
	ps = ( playerState_t * )( ( byte * ) sv.gameClients + sv.gameClientSize * ( num ) );

	return ps;
}

svEntity_t     *SV_SvEntityForGentity( sharedEntity_t *gEnt )
{
	if ( !gEnt || gEnt->s.number < 0 || gEnt->s.number >= MAX_GENTITIES )
	{
		Com_Error( ERR_DROP, "SV_SvEntityForGentity: bad gEnt" );
	}

	return &sv.svEntities[ gEnt->s.number ];
}

sharedEntity_t *SV_GEntityForSvEntity( svEntity_t *svEnt )
{
	int num;

	num = svEnt - sv.svEntities;
	return SV_GentityNum( num );
}

/*
===============
SV_GameSendServerCommand

Sends a command string to a client
===============
*/
void SV_GameSendServerCommand( int clientNum, const char *text )
{
	if ( clientNum == -1 )
	{
		SV_SendServerCommand( NULL, "%s", text );
	}
	else if ( clientNum == -2 )
	{
		SV_PrintTranslatedText( text, qfalse, qfalse );
	}
	else
	{
		if ( clientNum < 0 || clientNum >= sv_maxclients->integer )
		{
			return;
		}

		SV_SendServerCommand( svs.clients + clientNum, "%s", text );
	}
}

/*
===============
SV_GameDropClient

Disconnects the client with a message
===============
*/
void SV_GameDropClient( int clientNum, const char *reason )
{
	if ( clientNum < 0 || clientNum >= sv_maxclients->integer )
	{
		return;
	}

	SV_DropClient( svs.clients + clientNum, reason );
}

/*
===============
SV_RSAGenMsg

Generate an encrypted RSA message
===============
*/
int SV_RSAGenMsg( const char *pubkey, char *cleartext, char *encrypted )
{
	struct rsa_public_key public_key;

	mpz_t                 message;
	unsigned char         buffer[ RSA_KEY_LENGTH / 8 - 11 ];
	int                   retval;
	Com_RandomBytes( buffer, RSA_KEY_LENGTH / 8 - 11 );
	nettle_mpz_init_set_str_256_u( message, RSA_KEY_LENGTH / 8 - 11, buffer );
	mpz_get_str( cleartext, 16, message );
	rsa_public_key_init( &public_key );
	mpz_set_ui( public_key.e, RSA_PUBLIC_EXPONENT );
	retval = mpz_set_str( public_key.n, pubkey, 16 ) + 1;

	if ( retval )
	{
		rsa_public_key_prepare( &public_key );
		retval = rsa_encrypt( &public_key, NULL, qnettle_random, RSA_KEY_LENGTH / 8 - 11, buffer, message );
	}

	rsa_public_key_clear( &public_key );
	mpz_get_str( encrypted, 16, message );
	mpz_clear( message );
	return retval;
}

/*
=================
SV_SetBrushModel

sets mins and maxs for inline bmodels
=================
*/
void SV_SetBrushModel( sharedEntity_t *ent, const char *name )
{
	clipHandle_t h;
	vec3_t       mins, maxs;

	if ( !name )
	{
		Com_Error( ERR_DROP, "SV_SetBrushModel: NULL for #%i", ent->s.number );
	}

	if ( name[ 0 ] != '*' )
	{
		Com_Error( ERR_DROP, "SV_SetBrushModel: %s of #%i isn't a brush model", name, ent->s.number );
	}

	ent->s.modelindex = atoi( name + 1 );

	h = CM_InlineModel( ent->s.modelindex );
	CM_ModelBounds( h, mins, maxs );
	VectorCopy( mins, ent->r.mins );
	VectorCopy( maxs, ent->r.maxs );
	ent->r.bmodel = qtrue;

	ent->r.contents = -1; // we don't know exactly what is in the brushes
}

/*
=================
SV_inPVS

Also checks portalareas so that doors block sight
=================
*/
qboolean SV_inPVS( const vec3_t p1, const vec3_t p2 )
{
	int  leafnum;
	int  cluster;
	int  area1, area2;
	byte *mask;

	leafnum = CM_PointLeafnum( p1 );
	cluster = CM_LeafCluster( leafnum );
	area1 = CM_LeafArea( leafnum );
	mask = CM_ClusterPVS( cluster );

	leafnum = CM_PointLeafnum( p2 );
	cluster = CM_LeafCluster( leafnum );
	area2 = CM_LeafArea( leafnum );

	if ( mask && ( !( mask[ cluster >> 3 ] & ( 1 << ( cluster & 7 ) ) ) ) )
	{
		return qfalse;
	}

	if ( !CM_AreasConnected( area1, area2 ) )
	{
		return qfalse; // a door blocks sight
	}

	return qtrue;
}

/*
=================
SV_inPVSIgnorePortals

Does NOT check portalareas
=================
*/
qboolean SV_inPVSIgnorePortals( const vec3_t p1, const vec3_t p2 )
{
	int  leafnum;
	int  cluster;
//	int             area1, area2; //unused
	byte *mask;

	leafnum = CM_PointLeafnum( p1 );
	cluster = CM_LeafCluster( leafnum );
//	area1 = CM_LeafArea(leafnum); //Doesn't modify anything.

	mask = CM_ClusterPVS( cluster );
	leafnum = CM_PointLeafnum( p2 );
	cluster = CM_LeafCluster( leafnum );
//	area2 = CM_LeafArea(leafnum); //Doesn't modify anything.

	if ( mask && ( !( mask[ cluster >> 3 ] & ( 1 << ( cluster & 7 ) ) ) ) )
	{
		return qfalse;
	}

	return qtrue;
}

/*
========================
SV_AdjustAreaPortalState
========================
*/
void SV_AdjustAreaPortalState( sharedEntity_t *ent, qboolean open )
{
	svEntity_t *svEnt;

	svEnt = SV_SvEntityForGentity( ent );

	if ( svEnt->areanum2 == -1 )
	{
		return;
	}

	CM_AdjustAreaPortalState( svEnt->areanum, svEnt->areanum2, open );
}

/*
==================
SV_EntityContact
==================
*/
qboolean SV_EntityContact( vec3_t mins, vec3_t maxs, const sharedEntity_t *gEnt, traceType_t type )
{
	const float  *origin, *angles;
	clipHandle_t ch;
	trace_t      trace;

	// check for exact collision
	origin = gEnt->r.currentOrigin;
	angles = gEnt->r.currentAngles;

	ch = SV_ClipHandleForEntity( gEnt );
	CM_TransformedBoxTrace( &trace, vec3_origin, vec3_origin, mins, maxs, ch, -1, origin, angles, type );

	return trace.startsolid;
}

/*
===============
SV_GetServerinfo

===============
*/
void SV_GetServerinfo( char *buffer, int bufferSize )
{
	if ( bufferSize < 1 )
	{
		Com_Error( ERR_DROP, "SV_GetServerinfo: bufferSize == %i", bufferSize );
	}

	Q_strncpyz( buffer, Cvar_InfoString( CVAR_SERVERINFO | CVAR_SERVERINFO_NOUPDATE, qfalse ), bufferSize );
}

/*
===============
SV_LocateGameData

===============
*/
void SV_LocateGameData( const NaCl::SharedMemoryPtr& shmRegion, int numGEntities, int sizeofGEntity_t,
                        int sizeofGameClient )
{
	if ( !shmRegion )
		Com_Error( ERR_DROP, "SV_LocateGameData: Failed to map shared memory region" );
	if ( numGEntities < 0 || sizeofGEntity_t < 0 || sizeofGameClient < 0 )
		Com_Error( ERR_DROP, "SV_LocateGameData: Invalid game data parameters" );
	if ( shmRegion.GetSize() < numGEntities * sizeofGEntity_t + sv_maxclients->integer * sizeofGameClient )
		Com_Error( ERR_DROP, "SV_LocateGameData: Shared memory region too small" );

	char* base = static_cast<char*>(shmRegion.Get());
	sv.gentities = reinterpret_cast<sharedEntity_t*>(base);
	sv.gentitySize = sizeofGEntity_t;
	sv.num_entities = numGEntities;

	sv.gameClients = reinterpret_cast<playerState_t*>(base + numGEntities * sizeofGEntity_t);
	sv.gameClientSize = sizeofGameClient;
}

/*
===============
SV_GetUsercmd

===============
*/
void SV_GetUsercmd( int clientNum, usercmd_t *cmd )
{
	if ( clientNum < 0 || clientNum >= sv_maxclients->integer )
	{
		Com_Error( ERR_DROP, "SV_GetUsercmd: bad clientNum:%i", clientNum );
	}

	*cmd = svs.clients[ clientNum ].lastUsercmd;
}

/*
====================
SV_SendBinaryMessage
====================
*/
static void SV_SendBinaryMessage( int cno, char *buf, int buflen )
{
	if ( cno < 0 || cno >= sv_maxclients->integer )
	{
		Com_Error( ERR_DROP, "SV_SendBinaryMessage: bad client %i", cno );
	}

	if ( buflen < 0 || buflen > MAX_BINARY_MESSAGE )
	{
		Com_Error( ERR_DROP, "SV_SendBinaryMessage: bad length %i", buflen );
	}

	svs.clients[ cno ].binaryMessageLength = buflen;
	memcpy( svs.clients[ cno ].binaryMessage, buf, buflen );
}

/*
====================
SV_BinaryMessageStatus
====================
*/
static int SV_BinaryMessageStatus( int cno )
{
	if ( cno < 0 || cno >= sv_maxclients->integer )
	{
		return qfalse;
	}

	if ( svs.clients[ cno ].binaryMessageLength == 0 )
	{
		return MESSAGE_EMPTY;
	}

	if ( svs.clients[ cno ].binaryMessageOverflowed )
	{
		return MESSAGE_WAITING_OVERFLOW;
	}

	return MESSAGE_WAITING;
}

/*
====================
SV_GameBinaryMessageReceived
====================
*/
void SV_GameBinaryMessageReceived( int cno, const char *buf, int buflen, int commandTime )
{
	gvm.GameMessageRecieved( cno, buf, buflen, commandTime );
}

//==============================================

extern int S_RegisterSound( const char *name, qboolean compressed );
extern int S_GetSoundLength( sfxHandle_t sfxHandle );

/*
====================
SV_GetTimeString

Returns 0 if we have a representable time
Truncation is ignored
====================
*/
static void SV_GetTimeString( char *buffer, int length, const char *format, const qtime_t *tm )
{
	if ( tm )
	{
		struct tm t;

		t.tm_sec   = tm->tm_sec;
		t.tm_min   = tm->tm_min;
		t.tm_hour  = tm->tm_hour;
		t.tm_mday  = tm->tm_mday;
		t.tm_mon   = tm->tm_mon;
		t.tm_year  = tm->tm_year;
		t.tm_wday  = tm->tm_wday;
		t.tm_yday  = tm->tm_yday;
		t.tm_isdst = tm->tm_isdst;

		strftime ( buffer, length, format, &t );
	}
	else
	{
		strftime( buffer, length, format, gmtime( NULL ) );
	}
}

/*
===============
SV_ShutdownGameProgs

Called every time a map changes
===============
*/
void SV_ShutdownGameProgs( void )
{
	if ( !gvm.IsActive() )
	{
		return;
	}

	gvm.GameShutdown( qfalse );
	gvm.Free();

	if ( sv_newGameShlib->string[ 0 ] )
	{
		FS_Rename( sv_newGameShlib->string, "game" DLL_EXT );
		Cvar_Set( "sv_newGameShlib", "" );
	}
}

/*
==================
SV_InitGameVM

Called for both a full init and a restart
==================
*/
static void SV_InitGameVM( qboolean restart )
{
	int i;

	// start the entity parsing at the beginning
	sv.entityParsePoint = CM_EntityString();

	// clear all gentity pointers that might still be set from
	// a previous level
	for ( i = 0; i < sv_maxclients->integer; i++ )
	{
		svs.clients[ i ].gentity = NULL;
	}

	// use the current msec count for a random seed
	// init for this gamestate
	gvm.GameInit( svs.time, Com_Milliseconds(), restart );
}

/*
===================
SV_RestartGameProgs

Called on a map_restart, but not on a map change
===================
*/
void SV_RestartGameProgs( void )
{
	if ( !gvm.IsActive() )
	{
		return;
	}

	gvm.GameShutdown( qtrue );

#ifdef USE_PHYSICS
	CMod_PhysicsClearBodies();
#endif

	gvm.Free();

	// Check ABI version
	int version = gvm.Create( "game", ( VM::Type ) Cvar_VariableIntegerValue( "vm_game" ) );
	if ( version != GAME_ABI_VERSION )
		Com_Error( ERR_DROP, "Game ABI mismatch, expected %d, got %d", GAME_ABI_VERSION, version );

	SV_InitGameVM( qtrue );
}

/*
===============
SV_InitGameProgs

Called on a map change, not on a map_restart
===============
*/
void SV_InitGameProgs( void )
{
	sv.num_tagheaders = 0;
	sv.num_tags = 0;

	// load the game module
	// Check ABI version
	int version = gvm.Create( "game", ( VM::Type ) Cvar_VariableIntegerValue( "vm_game" ) );
	if ( version != GAME_ABI_VERSION )
		Com_Error( ERR_DROP, "Game ABI mismatch, expected %d, got %d", GAME_ABI_VERSION, version );

	SV_InitGameVM( qfalse );
}

/*
====================
SV_GameCommand

See if the current console command is claimed by the game
====================
*/
qboolean SV_GameCommand( void )
{
	if ( sv.state != SS_GAME )
	{
		return qfalse;
	}

	return gvm.GameConsoleCommand();
}

/*
====================
SV_GetTag

  return qfalse if unable to retrieve tag information for this client
====================
*/
extern qboolean CL_GetTag( int clientNum, const char *tagname, orientation_t * org );

qboolean SV_GetTag( int clientNum, int tagFileNumber, const char *tagname, orientation_t * org )
{
	int i;

	if ( tagFileNumber > 0 && tagFileNumber <= sv.num_tagheaders )
	{
		for ( i = sv.tagHeadersExt[ tagFileNumber - 1 ].start;
		      i < sv.tagHeadersExt[ tagFileNumber - 1 ].start + sv.tagHeadersExt[ tagFileNumber - 1 ].count; i++ )
		{
			if ( !Q_stricmp( sv.tags[ i ].name, tagname ) )
			{
				VectorCopy( sv.tags[ i ].origin, org ->origin );
				VectorCopy( sv.tags[ i ].axis[ 0 ], org ->axis[ 0 ] );
				VectorCopy( sv.tags[ i ].axis[ 1 ], org ->axis[ 1 ] );
				VectorCopy( sv.tags[ i ].axis[ 2 ], org ->axis[ 2 ] );
				return qtrue;
			}
		}
	}

	// Gordon: let's try and remove the inconsistency between ded/non-ded servers...
	// Gordon: bleh, some code in clientthink_real really relies on this working on player models...
#ifndef DEDICATED // TTimo: dedicated only binary defines DEDICATED

	if ( com_dedicated->integer )
	{
		return qfalse;
	}

	return CL_GetTag( clientNum, tagname, org );
#else
	return qfalse;
#endif
}

void GameVM::GameInit(int levelTime, int randomSeed, qboolean restart)
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(GAME_INIT);
	input.WriteInt(levelTime);
	input.WriteInt(randomSeed);
	input.WriteInt(restart);
	DoRPC(input, output);
}

void GameVM::GameShutdown(qboolean restart)
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(GAME_SHUTDOWN);
	input.WriteInt(restart);

	// Ignore socket errors when shutting down, in case the remote process
	// has been killed and we are shutting down because of an error.
	DoRPC(input, output, true);

	// Release the shared memory region
	shmRegion = nullptr;
}

qboolean GameVM::GameClientConnect(char* reason, size_t size, int clientNum, qboolean firstTime, qboolean isBot)
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(GAME_CLIENT_CONNECT);
	input.WriteInt(clientNum);
	input.WriteInt(firstTime);
	input.WriteInt(isBot);
	DoRPC(input, output);
	qboolean denied = output.ReadInt();
	if (denied)
		Q_strncpyz(reason, output.ReadString(), size);
	return denied;
}

void GameVM::GameClientBegin(int clientNum)
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(GAME_CLIENT_BEGIN);
	input.WriteInt(clientNum);
	DoRPC(input, output);
}

void GameVM::GameClientUserInfoChanged(int clientNum)
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(GAME_CLIENT_USERINFO_CHANGED);
	input.WriteInt(clientNum);
	DoRPC(input, output);
}

void GameVM::GameClientDisconnect(int clientNum)
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(GAME_CLIENT_DISCONNECT);
	input.WriteInt(clientNum);
	DoRPC(input, output);
}

void GameVM::GameClientCommand(int clientNum)
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(GAME_CLIENT_COMMAND);
	input.WriteInt(clientNum);
	DoRPC(input, output);
}

void GameVM::GameClientThink(int clientNum)
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(GAME_CLIENT_THINK);
	input.WriteInt(clientNum);
	DoRPC(input, output);
}

void GameVM::GameRunFrame(int levelTime)
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(GAME_RUN_FRAME);
	input.WriteInt(levelTime);
	DoRPC(input, output);
}

qboolean GameVM::GameConsoleCommand()
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(GAME_CONSOLE_COMMAND);
	DoRPC(input, output);
	return output.ReadInt();
}

qboolean GameVM::GameSnapshotCallback(int entityNum, int clientNum)
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(GAME_SNAPSHOT_CALLBACK);
	input.WriteInt(entityNum);
	input.WriteInt(clientNum);
	DoRPC(input, output);
	return output.ReadInt();
}

void GameVM::BotAIStartFrame(int levelTime)
{
	RPC::Writer input;
	RPC::Reader output;
	input.WriteInt(BOTAI_START_FRAME);
	input.WriteInt(levelTime);
	DoRPC(input, output);
}

void GameVM::GameMessageRecieved(int clientNum, const char *buffer, int bufferSize, int commandTime)
{
	Com_Error(ERR_DROP, "GameVM::GameMessageRecieved not implemented");
}

void GameVM::Syscall(int index, RPC::Reader& inputs, RPC::Writer& outputs)
{
	switch (index) {
	case G_PRINT:
		Com_Printf("%s", inputs.ReadString());
		break;

	case G_ERROR:
		Com_Error(ERR_DROP, "%s", inputs.ReadString());

	case G_LOG:
		Com_Error(ERR_DROP, "trap_Log not implemented");

	case G_MILLISECONDS:
		outputs.WriteInt(Sys_Milliseconds());
		break;

	case G_CVAR_REGISTER:
	{
		vmCvar_t cvar;
		inputs.Read(&cvar, sizeof(vmCvar_t));
		const char* name = inputs.ReadString();
		const char* defaultValue = inputs.ReadString();
		int flags = inputs.ReadInt();
		Cvar_Register(&cvar, name, defaultValue, flags);
		outputs.Write(&cvar, sizeof(vmCvar_t));
		break;
	}

	case G_CVAR_UPDATE:
	{
		vmCvar_t cvar;
		inputs.Read(&cvar, sizeof(vmCvar_t));
		Cvar_Update(&cvar);
		outputs.Write(&cvar, sizeof(vmCvar_t));
		break;
	}

	case G_CVAR_SET:
	{
		const char* name = inputs.ReadString();
		const char* value = inputs.ReadString();
		Cvar_Set(name, value);
		break;
	}

	case G_CVAR_VARIABLE_INTEGER_VALUE:
		outputs.WriteInt(Cvar_VariableIntegerValue(inputs.ReadString()));
		break;

	case G_CVAR_VARIABLE_STRING_BUFFER:
		outputs.WriteString(Cvar_VariableString(inputs.ReadString()));
		break;

	case G_ARGC:
		outputs.WriteInt(Cmd_Argc());
		break;

	case G_ARGV:
		outputs.WriteString(Cmd_Argv(inputs.ReadInt()));
		break;

	case G_SEND_CONSOLE_COMMAND:
	{
		int exec_when = inputs.ReadInt();
		const char* text = inputs.ReadString();
		Cbuf_ExecuteText(exec_when, text);
		break;
	}

	case G_FS_FOPEN_FILE:
	{
		const char* filename = inputs.ReadString();
		fsMode_t mode = static_cast<fsMode_t>(inputs.ReadInt());
		fileHandle_t f;
		outputs.WriteInt(FS_FOpenFileByMode(filename, &f, mode));
		outputs.WriteInt(f);
		break;
	}

	case G_FS_READ:
	{
		fileHandle_t f= inputs.ReadInt();
		int len = inputs.ReadInt();
		std::unique_ptr<char[]> buffer(new char[len]);
		FS_Read(buffer.get(), len, f);
		outputs.WriteInt(len);
		outputs.Write(buffer.get(), len);
		break;
	}

	case G_FS_WRITE:
	{
		fileHandle_t f= inputs.ReadInt();
		int len = inputs.ReadInt();
		const void* buffer = inputs.ReadInline(len);
		outputs.WriteInt(FS_Write(buffer, len, f));
		break;
	}

	case G_FS_RENAME:
	{
		const char* from = inputs.ReadString();
		const char* to = inputs.ReadString();
		FS_Rename(from, to);
		break;
	}

	case G_FS_FCLOSE_FILE:
		FS_FCloseFile(inputs.ReadInt());
		break;

	case G_FS_GETFILELIST:
	{
		const char* path = inputs.ReadString();
		const char* extension = inputs.ReadString();
		int len = inputs.ReadInt();
		std::unique_ptr<char[]> buffer(new char[len]);
		outputs.WriteInt(FS_GetFileList(path, extension, buffer.get(), len));
		outputs.WriteInt(len);
		outputs.Write(buffer.get(), len);
		break;
	}

	case G_LOCATE_GAME_DATA:
	{
		NaCl::IPCHandle sharedMemory = inputs.ReadHandle();
		shmRegion = sharedMemory.Map();
		int numEntities = inputs.ReadInt();
		int entitySize = inputs.ReadInt();
		int playerSize = inputs.ReadInt();
		SV_LocateGameData(shmRegion, numEntities, entitySize, playerSize);
		break;
	}

	case G_DROP_CLIENT:
	{
		int clientNum = inputs.ReadInt();
		const char* reason = inputs.ReadString();
		SV_GameDropClient(clientNum, reason);
		break;
	}

	case G_SEND_SERVER_COMMAND:
	{
		int clientNum = inputs.ReadInt();
		const char* text = inputs.ReadString();
		SV_GameSendServerCommand(clientNum, text);
		break;
	}

	case G_LINKENTITY:
		SV_LinkEntity(SV_GentityNum(inputs.ReadInt()));
		break;

	case G_UNLINKENTITY:
		SV_UnlinkEntity(SV_GentityNum(inputs.ReadInt()));
		break;

	case G_ENTITIES_IN_BOX:
	{
		vec3_t mins, maxs;
		inputs.Read(mins, sizeof(vec3_t));
		inputs.Read(maxs, sizeof(vec3_t));
		int len = inputs.ReadInt();
		std::unique_ptr<int[]> buffer(new int[len]);
		len = SV_AreaEntities(mins, maxs, buffer.get(), len);
		outputs.WriteInt(len);
		outputs.Write(buffer.get(), len * sizeof(int));
		break;
	}

	case G_ENTITY_CONTACT:
	{
		vec3_t mins, maxs;
		inputs.Read(mins, sizeof(vec3_t));
		inputs.Read(maxs, sizeof(vec3_t));
		const sharedEntity_t* ent = SV_GentityNum(inputs.ReadInt());
		outputs.WriteInt(SV_EntityContact(mins, maxs, ent, TT_AABB));
		break;
	}

	case G_ENTITY_CONTACTCAPSULE:
	{
		vec3_t mins, maxs;
		inputs.Read(mins, sizeof(vec3_t));
		inputs.Read(maxs, sizeof(vec3_t));
		const sharedEntity_t* ent = SV_GentityNum(inputs.ReadInt());
		outputs.WriteInt(SV_EntityContact(mins, maxs, ent, TT_CAPSULE));
		break;
	}

	case G_TRACE:
	{
		vec3_t start, mins, maxs, end;
		inputs.Read(start, sizeof(vec3_t));
		inputs.Read(mins, sizeof(vec3_t));
		inputs.Read(maxs, sizeof(vec3_t));
		inputs.Read(end, sizeof(vec3_t));
		int passEntityNum = inputs.ReadInt();
		int contentmask = inputs.ReadInt();
		trace_t result;
		SV_Trace(&result, start, mins, maxs, end, passEntityNum, contentmask, TT_AABB);
		outputs.Write(&result, sizeof(trace_t));
		break;
	}

	case G_TRACECAPSULE:
	{
		vec3_t start, mins, maxs, end;
		inputs.Read(start, sizeof(vec3_t));
		inputs.Read(mins, sizeof(vec3_t));
		inputs.Read(maxs, sizeof(vec3_t));
		inputs.Read(end, sizeof(vec3_t));
		int passEntityNum = inputs.ReadInt();
		int contentmask = inputs.ReadInt();
		trace_t result;
		SV_Trace(&result, start, mins, maxs, end, passEntityNum, contentmask, TT_CAPSULE);
		outputs.Write(&result, sizeof(trace_t));
		break;
	}

	case G_POINT_CONTENTS:
	{
		vec3_t p;
		inputs.Read(p, sizeof(p));
		int passEntityNum = inputs.ReadInt();
		outputs.WriteInt(SV_PointContents(p, passEntityNum));
		break;
	}

	case G_SET_BRUSH_MODEL:
	{
		sharedEntity_t* ent = SV_GentityNum(inputs.ReadInt());
		const char* name = inputs.ReadString();
		SV_SetBrushModel(ent, name);
		break;
	}

	case G_IN_PVS:
	{
		vec3_t p1, p2;
		inputs.Read(p1, sizeof(vec3_t));
		inputs.Read(p2, sizeof(vec3_t));
		outputs.WriteInt(SV_inPVS(p1, p2));
		break;
	}

	case G_IN_PVS_IGNORE_PORTALS:
	{
		vec3_t p1, p2;
		inputs.Read(p1, sizeof(vec3_t));
		inputs.Read(p2, sizeof(vec3_t));
		outputs.WriteInt(SV_inPVSIgnorePortals(p1, p2));
		break;
	}

	case G_SET_CONFIGSTRING:
	{
		int index = inputs.ReadInt();
		const char* val = inputs.ReadString();
		SV_SetConfigstring(index, val);
		break;
	}

	case G_GET_CONFIGSTRING:
	{
		int index = inputs.ReadInt();
		int len = inputs.ReadInt();
		std::unique_ptr<char[]> buffer(new char[len]);
		SV_GetConfigstring(index, buffer.get(), len);
		outputs.WriteString(buffer.get());
		break;
	}

	case G_SET_CONFIGSTRING_RESTRICTIONS:
		Com_Error(ERR_DROP, "SV_SetConfigstringRestrictions not implemented");

	case G_SET_USERINFO:
	{
		int index = inputs.ReadInt();
		const char* val = inputs.ReadString();
		SV_SetUserinfo(index, val);
		break;
	}

	case G_GET_USERINFO:
	{
		int index = inputs.ReadInt();
		int len = inputs.ReadInt();
		std::unique_ptr<char[]> buffer(new char[len]);
		SV_GetUserinfo(index, buffer.get(), len);
		outputs.WriteString(buffer.get());
		break;
	}

	case G_GET_SERVERINFO:
	{
		int len = inputs.ReadInt();
		std::unique_ptr<char[]> buffer(new char[len]);
		SV_GetServerinfo(buffer.get(), len);
		outputs.WriteString(buffer.get());
		break;
	}

	case G_ADJUST_AREA_PORTAL_STATE:
	{
		sharedEntity_t* ent = SV_GentityNum(inputs.ReadInt());
		qboolean open = inputs.ReadInt();
		SV_AdjustAreaPortalState(ent, open);
		break;
	}

	case G_AREAS_CONNECTED:
	{
		int area1 = inputs.ReadInt();
		int area2 = inputs.ReadInt();
		outputs.WriteInt(CM_AreasConnected(area1, area2));
		break;
	}

	case G_BOT_ALLOCATE_CLIENT:
		outputs.WriteInt(SV_BotAllocateClient(inputs.ReadInt()));
		break;

	case G_BOT_FREE_CLIENT:
		SV_BotFreeClient(inputs.ReadInt());
		break;

	case BOT_GET_CONSOLE_MESSAGE:
	{
		int client = inputs.ReadInt();
		int len = inputs.ReadInt();
		std::unique_ptr<char[]> buffer(new char[len]);
		outputs.WriteInt(SV_BotGetConsoleMessage(client, buffer.get(), len));
		outputs.WriteString(buffer.get());
	}

	case G_GET_USERCMD:
	{
		int index = inputs.ReadInt();
		usercmd_t cmd;
		SV_GetUsercmd(index, &cmd);
		outputs.Write(&cmd, sizeof(usercmd_t));
		break;
	}

	case G_GET_ENTITY_TOKEN:
	{
		const char *s = COM_Parse(&sv.entityParsePoint);
		if (!sv.entityParsePoint && !s[0])
			outputs.WriteInt(qfalse);
		else
			outputs.WriteInt(qtrue);
		outputs.WriteString(s);
		break;
	}

	case G_GM_TIME:
	{
		qtime_t t;
		outputs.WriteInt(Com_GMTime(&t));
		outputs.Write(&t, sizeof(qtime_t));
		break;
	}

	case G_SNAPVECTOR:
	{
		vec3_t v;
		inputs.Read(v, sizeof(vec3_t));
		Q_SnapVector(v);
		outputs.Write(v, sizeof(vec3_t));
		break;
	}

	case G_SEND_GAMESTAT:
		SV_MasterGameStat(inputs.ReadString());
		break;

	case G_ADDCOMMAND:
		Cmd_AddCommand(inputs.ReadString(), NULL );
		break;

	case G_REMOVECOMMAND:
		Cmd_RemoveCommand(inputs.ReadString());
		break;

	case G_GETTAG:
	{
		int clientNum = inputs.ReadInt();
		int tagFileNumber = inputs.ReadInt();
		const char* tagName = inputs.ReadString();
		orientation_t org;
		outputs.WriteInt(SV_GetTag(clientNum, tagFileNumber, tagName, &org));
		outputs.Write(&org, sizeof(orientation_t));
		break;
	}

	case G_REGISTERTAG:
		outputs.WriteInt(SV_LoadTag(inputs.ReadString()));
		break;

	case G_REGISTERSOUND:
	{
		const char* name = inputs.ReadString();
		qboolean compressed = inputs.ReadInt();
		outputs.WriteInt(S_RegisterSound(name, compressed));
		break;
	}

	case G_GET_SOUND_LENGTH:
		outputs.WriteInt(S_GetSoundLength(inputs.ReadInt()));
		break;

	case G_PARSE_ADD_GLOBAL_DEFINE:
		outputs.WriteInt(Parse_AddGlobalDefine(inputs.ReadString()));
		break;

	case G_PARSE_LOAD_SOURCE:
		outputs.WriteInt(Parse_LoadSourceHandle(inputs.ReadString()));
		break;

	case G_PARSE_FREE_SOURCE:
		outputs.WriteInt(Parse_FreeSourceHandle(inputs.ReadInt()));
		break;

	case G_PARSE_READ_TOKEN:
	{
		pc_token_t token;
		outputs.WriteInt(Parse_ReadTokenHandle(inputs.ReadInt(), &token));
		outputs.Write(&token, sizeof(pc_token_t));
		break;
	}

	case G_PARSE_SOURCE_FILE_AND_LINE:
	{
		char buffer[128];
		int line;
		outputs.WriteInt(Parse_SourceFileAndLine(inputs.ReadInt(), buffer, &line));
		outputs.WriteString(buffer);
		outputs.WriteInt(line);
		break;
	}

// ===

	case G_ADD_PHYSICS_ENTITY:
#ifdef USE_PHYSICS
		CMod_PhysicsAddEntity(SV_GentityNum(inputs.ReadInt()));
#endif
		break;

	case G_ADD_PHYSICS_STATIC:
#ifdef USE_PHYSICS
		CMod_PhysicsAddStatic(SV_GentityNum(inputs.ReadInt()));
#endif
		break;

	case G_SENDMESSAGE:
	{
		int clientNum = inputs.ReadInt();
		int len = inputs.ReadInt();
		std::unique_ptr<char[]> buffer(new char[len]);
		inputs.Read(buffer.get(), len);
		SV_SendBinaryMessage(clientNum, buffer.get(), len);
		break;
	}

	case G_MESSAGESTATUS:
		outputs.WriteInt(SV_BinaryMessageStatus(inputs.ReadInt()));
		break;

	case G_RSA_GENMSG:
	{
		const char* pubkey = inputs.ReadString();
		char cleartext[RSA_STRING_LENGTH];
		char encrypted[RSA_STRING_LENGTH];
		outputs.WriteInt(SV_RSAGenMsg(pubkey, cleartext, encrypted));
		outputs.WriteString(cleartext);
		outputs.WriteString(encrypted);
		break;
	}

	case G_QUOTESTRING:
		outputs.WriteString(Cmd_QuoteString(inputs.ReadString()));
		break;

	case G_GENFINGERPRINT:
	{
		int keylen = inputs.ReadInt();
		const char* key = static_cast<const char*>(inputs.ReadInline(keylen));
		int len = inputs.ReadInt();
		std::unique_ptr<char[]> buffer(new char[len]);
		Com_MD5Buffer(key, keylen, buffer.get(), len);
		outputs.WriteString(buffer.get());
		break;
	}

	case G_GETPLAYERPUBKEY:
	{
		int clientNum = inputs.ReadInt();
		int len = inputs.ReadInt();
		std::unique_ptr<char[]> buffer(new char[len]);
		SV_GetPlayerPubkey(clientNum, buffer.get(), len);
		outputs.WriteString(buffer.get());
		break;
	}

	case G_GETTIMESTRING:
	{
		int len = inputs.ReadInt();
		std::unique_ptr<char[]> buffer(new char[len]);
		const char* format = inputs.ReadString();
		qtime_t t;
		inputs.Read(&t, sizeof(qtime_t));
		SV_GetTimeString(buffer.get(), len, format, &t);
		outputs.WriteString(buffer.get());
		break;
	}

	default:
		Com_Error(ERR_DROP, "Bad game system trap: %d", index);
	}
}

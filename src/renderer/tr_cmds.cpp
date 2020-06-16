#include "tr_local.h"

volatile renderCommandList_t	*renderCommandList;

volatile qboolean	renderThreadActive;


/*
=====================
R_PerformanceCounters
=====================
*/
void R_PerformanceCounters( void ) {
	if ( !r_speeds->integer ) {
		// clear the counters even if we aren't printing
		Com_Memset( &tr.pc, 0, sizeof( tr.pc ) );
		Com_Memset( &backEnd.pc, 0, sizeof( backEnd.pc ) );
		return;
	}

	if (r_speeds->integer == 1) {
		const float texSize = R_SumOfUsedImages( qfalse )/(8*1048576.0f)*(r_texturebits->integer?r_texturebits->integer:glConfig.colorBits);
		ri.Printf (PRINT_ALL, "%i/%i shdrs/srfs %i leafs %i vrts %i/%i tris %.2fMB tex %.2f dc\n",
			backEnd.pc.c_shaders, backEnd.pc.c_surfaces, tr.pc.c_leafs, backEnd.pc.c_vertexes,
			backEnd.pc.c_indexes/3, backEnd.pc.c_totalIndexes/3,
			texSize, backEnd.pc.c_overDraw / (float)(glConfig.vidWidth * glConfig.vidHeight) );
	} else if (r_speeds->integer == 2) {
		ri.Printf (PRINT_ALL, "(patch) %i sin %i sclip  %i sout %i bin %i bclip %i bout\n",
			tr.pc.c_sphere_cull_patch_in, tr.pc.c_sphere_cull_patch_clip, tr.pc.c_sphere_cull_patch_out,
			tr.pc.c_box_cull_patch_in, tr.pc.c_box_cull_patch_clip, tr.pc.c_box_cull_patch_out );
		ri.Printf (PRINT_ALL, "(md3) %i sin %i sclip  %i sout %i bin %i bclip %i bout\n",
			tr.pc.c_sphere_cull_md3_in, tr.pc.c_sphere_cull_md3_clip, tr.pc.c_sphere_cull_md3_out,
			tr.pc.c_box_cull_md3_in, tr.pc.c_box_cull_md3_clip, tr.pc.c_box_cull_md3_out );
	} else if (r_speeds->integer == 3) {
		ri.Printf (PRINT_ALL, "viewcluster: %i\n", tr.viewCluster );
	} else if (r_speeds->integer == 4) {
		if ( backEnd.pc.c_dlightVertexes ) {
			ri.Printf (PRINT_ALL, "dlight srf:%i  culled:%i  verts:%i  tris:%i\n",
				tr.pc.c_dlightSurfaces, tr.pc.c_dlightSurfacesCulled,
				backEnd.pc.c_dlightVertexes, backEnd.pc.c_dlightIndexes / 3 );
		}
	}
	else if (r_speeds->integer == 5 )
	{
		ri.Printf( PRINT_ALL, "zFar: %.0f\n", tr.viewParms.zFar );
	}
	else if (r_speeds->integer == 6 )
	{
		ri.Printf( PRINT_ALL, "flare adds:%i tests:%i renders:%i\n",
			backEnd.pc.c_flareAdds, backEnd.pc.c_flareTests, backEnd.pc.c_flareRenders );
	}
	else if (r_speeds->integer == 7) {
		const float texSize = R_SumOfUsedImages(qtrue) / (1048576.0f);
		const float backBuff= glConfig.vidWidth * glConfig.vidHeight * glConfig.colorBits / (8.0f * 1024*1024);
		const float depthBuff= glConfig.vidWidth * glConfig.vidHeight * glConfig.depthBits / (8.0f * 1024*1024);
		const float stencilBuff= glConfig.vidWidth * glConfig.vidHeight * glConfig.stencilBits / (8.0f * 1024*1024);
		ri.Printf (PRINT_ALL, "Tex MB %.2f + buffers %.2f MB = Total %.2fMB\n",
			texSize, backBuff*2+depthBuff+stencilBuff, texSize+backBuff*2+depthBuff+stencilBuff);
	}

	Com_Memset( &tr.pc, 0, sizeof( tr.pc ) );
	Com_Memset( &backEnd.pc, 0, sizeof( backEnd.pc ) );
}

/*
====================
R_IssueRenderCommands
====================
*/
int	c_blockedOnRender;
int	c_blockedOnMain;

void R_IssueRenderCommands( qboolean runPerformanceCounters ) {
	renderCommandList_t	*cmdList;

	cmdList = &backEndData->commands;
	assert(cmdList);
	// add an end-of-list command
	*(int *)(cmdList->cmds + cmdList->used) = RC_END_OF_LIST;

	// clear it out, in case this is a sync and not a buffer flip
	cmdList->used = 0;

	// at this point, the back end thread is idle, so it is ok
	// to look at it's performance counters
	if (runPerformanceCounters) {
		R_PerformanceCounters();
	}

	// actually start the commands going
	if (!tr.skipBackend) {
		// let it start on the new batch
		RB_ExecuteRenderCommands(cmdList->cmds);
	}
}


/*
====================
R_SyncRenderThread

Issue any pending commands and wait for them to complete.
====================
*/
void R_SyncRenderThread( void ) {
	if (!tr.registered) {
		return;
	}

	R_IssueRenderCommands(qfalse);
}

/*
============
R_GetCommandBufferReserved

make sure there is enough command space, waiting on the
render thread if needed.
============
*/
void *R_GetCommandBufferReserved( unsigned int bytes, int reservedBytes ) {
	renderCommandList_t	*cmdList;

	cmdList = &backEndData->commands;
	bytes = PAD( bytes, sizeof( void * ) );

	// always leave room for the end of list command
	if ( cmdList->used + bytes + sizeof( int ) + reservedBytes > MAX_RENDER_COMMANDS ) {
		if ( bytes > MAX_RENDER_COMMANDS - sizeof( int ) ) {
			ri.Error( ERR_FATAL, "R_GetCommandBuffer: bad size %i", bytes );
		}
		// if we run out of room, just start dropping commands
		return NULL;
	}

	cmdList->used += bytes;

	return cmdList->cmds + cmdList->used - bytes;
}

/*
============
R_GetCommandBuffer

returns NULL if there is not enough space for important commands
============
*/
void *R_GetCommandBuffer( unsigned int bytes ) {
	return R_GetCommandBufferReserved( bytes, PAD( sizeof ( swapBuffersCommand_t ), sizeof( void * ) ) );
}


/*
=============
R_AddDrawSurfCmd

=============
*/
void	R_AddDrawSurfCmd( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	drawSurfsCommand_t	*cmd;

	cmd = (drawSurfsCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_DRAW_SURFS;

	cmd->drawSurfs = drawSurfs;
	cmd->numDrawSurfs = numDrawSurfs;

	cmd->refdef = tr.refdef;
	cmd->viewParms = tr.viewParms;
}


/*
=============
RE_SetColor

Passing NULL will set the color to white
=============
*/
void	RE_SetColor( const vec4_t rgba ) {
	setColorCommand_t	*cmd;

	cmd = (setColorCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_SET_COLOR;
	if ( !rgba ) {
		static const float colorWhite[4] = { 1, 1, 1, 1 };

		rgba = colorWhite;
	}

	cmd->color[0] = rgba[0];
	cmd->color[1] = rgba[1];
	cmd->color[2] = rgba[2];
	cmd->color[3] = rgba[3];
}


/*
=============
RE_StretchPic

x, y, w and h are in virtual screen coordinates
xadjust is 640 / virtual screen width
yadjust is 480 / virtual screen height
=============
*/
void RE_StretchPic ( float x, float y, float w, float h, float s1, float t1,
	float s2, float t2, qhandle_t hShader, float xadjust, float yadjust )
{
	stretchPicCommand_t	*cmd;

	cmd = (stretchPicCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_STRETCH_PIC;
	cmd->shader = R_GetShaderByHandle( hShader );
	cmd->x = x * xadjust;
	cmd->y = y * yadjust;
	cmd->w = w * xadjust;
	cmd->h = h * yadjust;
	cmd->s1 = s1;
	cmd->t1 = t1;
	cmd->s2 = s2;
	cmd->t2 = t2;
}

/*
=============
RE_RotatePic

x, y, w and h are in virtual screen coordinates
xadjust is 640 / virtual screen width
yadjust is 480 / virtual screen height
=============
*/
void RE_RotatePic ( float x, float y, float w, float h, float s1, float t1,
	float s2, float t2,float a, qhandle_t hShader, float xadjust, float yadjust ) {
	transformPicCommand_t	*cmd;
	float s, c;

	cmd = (transformPicCommand_t *) R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}

	a = DEG2RAD( a );
	s = sinf( a );
	c = cosf( a );

	cmd->commandId = RC_TRANSFORM_PIC;
	cmd->shader = R_GetShaderByHandle( hShader );
	cmd->m[0][0] = xadjust * w * c;
	cmd->m[0][1] = xadjust * h * -s;
	cmd->m[1][0] = yadjust * w * s;
	cmd->m[1][1] = yadjust * h * c;
	// rotate around top-right corner
	cmd->x = xadjust * x - cmd->m[0][0] + xadjust * w;
	cmd->y = yadjust * y - cmd->m[1][0];
	cmd->s1 = s1;
	cmd->t1 = t1;
	cmd->s2 = s2;
	cmd->t2 = t2;
}

/*
=============
RE_RotatePic2

x, y, w and h are in virtual screen coordinates
xadjust is 640 / virtual screen width
yadjust is 480 / virtual screen height
=============
*/
void RE_RotatePic2 ( float x, float y, float w, float h, float s1, float t1,
	float s2, float t2,float a, qhandle_t hShader, float xadjust, float yadjust ) {
	transformPicCommand_t	*cmd;
	float s, c;

	cmd = (transformPicCommand_t *) R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}

	a = DEG2RAD( a );
	s = sinf( a );
	c = cosf( a );

	cmd->commandId = RC_TRANSFORM_PIC;
	cmd->shader = R_GetShaderByHandle( hShader );
	cmd->m[0][0] = xadjust * w * c;
	cmd->m[0][1] = xadjust * h * -s;
	cmd->m[1][0] = yadjust * w * s;
	cmd->m[1][1] = yadjust * h * c;
	cmd->x = xadjust * x - 0.5f * (cmd->m[0][0] + cmd->m[0][1]);
	cmd->y = yadjust * y - 0.5f * (cmd->m[1][0] + cmd->m[1][1]);
	cmd->s1 = s1;
	cmd->t1 = t1;
	cmd->s2 = s2;
	cmd->t2 = t2;
}

/*
====================
RE_BeginFrame

If running in stereo, RE_BeginFrame will be called twice
for each RE_EndFrame
====================
*/
void RE_BeginFrame( stereoFrame_t stereoFrame, qboolean skipBackend ) {
	drawBufferCommand_t	*cmd;

	if ( !tr.registered ) {
		return;
	}
	glState.finishCalled = qfalse;

	tr.frameCount++;
	tr.frameSceneNum = 0;

	tr.skipBackend = (qboolean)(r_skipBackEnd->integer || skipBackend);

	//
	// do overdraw measurement
	//
	if ( r_measureOverdraw->integer )
	{
		if ( glConfig.stencilBits < 4 )
		{
			ri.Printf( PRINT_ALL, "Warning: not enough stencil bits to measure overdraw: %d\n", glConfig.stencilBits );
			ri.Cvar_Set( "r_measureOverdraw", "0" );
			r_measureOverdraw->modified = qfalse;
		}
		else if ( r_shadows->integer == 2 )
		{
			ri.Printf( PRINT_ALL, "Warning: stencil shadows and overdraw measurement are mutually exclusive\n" );
			ri.Cvar_Set( "r_measureOverdraw", "0" );
			r_measureOverdraw->modified = qfalse;
		}
		else
		{
			R_SyncRenderThread();
			qglEnable( GL_STENCIL_TEST );
			qglStencilMask( ~0U );
			qglClearStencil( 0U );
			qglStencilFunc( GL_ALWAYS, 0U, ~0U );
			qglStencilOp( GL_KEEP, GL_INCR, GL_INCR );
		}
		r_measureOverdraw->modified = qfalse;
	}
	else
	{
		// this is only reached if it was on and is now off
		if ( r_measureOverdraw->modified ) {
			R_SyncRenderThread();
			qglDisable( GL_STENCIL_TEST );
		}
		r_measureOverdraw->modified = qfalse;
	}

	//
	// texturemode stuff
	//
	if ( r_textureMode->modified || r_ext_texture_filter_anisotropic->modified) {
		R_SyncRenderThread();
		GL_TextureMode( r_textureMode->string );
		r_textureMode->modified = qfalse;
		r_ext_texture_filter_anisotropic->modified = qfalse;
	}

	if ( glConfig.textureLODBiasAvailable && r_textureLODBias->modified ) {
		qglTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, r_textureLODBias->value );
		r_textureLODBias->modified = qfalse;
	}

	//
	// gamma stuff
	//
	if ( r_gamma->modified ) {
		r_gamma->modified = qfalse;

		R_SyncRenderThread();
		R_SetColorMappings();
	}

	//
	// console font stuff
	//
	if ( r_consoleFont->modified ) {
		r_consoleFont->modified = qfalse;

		if ( r_consoleFont->integer == 1 )
			R_RemapShader("gfx/2d/charsgrid_med", "gfx/2d/code_new_roman", 0);
		else if ( r_consoleFont->integer == 2 )
			R_RemapShader("gfx/2d/charsgrid_med", "gfx/2d/mplus_1m_bold", 0);
		else
			R_RemapShader("gfx/2d/charsgrid_med", "gfx/2d/charsgrid_med", 0);
	}

	// check for errors
	if ( !r_ignoreGLErrors->integer ) {
		int	err;

		R_SyncRenderThread();
		if ( ( err = qglGetError() ) != GL_NO_ERROR ) {
			ri.Error( ERR_FATAL, "RE_BeginFrame() - glGetError() failed (0x%x)!", err );
		}
	}

	//
	// draw buffer stuff
	//
	cmd = (drawBufferCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_DRAW_BUFFER;

	if ( glConfig.stereoEnabled ) {
		if ( stereoFrame == STEREO_LEFT ) {
			cmd->buffer = (int)GL_BACK_LEFT;
		} else if ( stereoFrame == STEREO_RIGHT ) {
			cmd->buffer = (int)GL_BACK_RIGHT;
		} else {
			ri.Error( ERR_FATAL, "RE_BeginFrame: Stereo is enabled, but stereoFrame was %i", stereoFrame );
		}
	} else {
		if ( stereoFrame != STEREO_CENTER ) {
			ri.Error( ERR_FATAL, "RE_BeginFrame: Stereo is disabled, but stereoFrame was %i", stereoFrame );
		}
		if ( !Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) ) {
			cmd->buffer = (int)GL_FRONT;
		} else {
			cmd->buffer = (int)GL_BACK;
		}
	}
}

/*
=============
RE_EndFrame

This must be called when drawing is done after RE_BeginFrame
=============
*/
void RE_EndFrame( void ) {
	if (r_gammamethod->integer == GAMMA_POSTPROCESSING) {
		RE_GammaCorrection();
	}
}

/*
=============
RE_SwapBuffers

Returns the number of msec spent in the back end
=============
*/
void RE_SwapBuffers( int *frontEndMsec, int *backEndMsec ) {
	swapBuffersCommand_t	*cmd;

	if ( !tr.registered ) {
		return;
	}

	if (tr.screenshotTGA) {
		tr.screenshotTGA = qfalse;
		RE_TakeScreenshotTGA(tr.screenshotTGAName, tr.screenshotTGASilent);
	}

	if (tr.screenshotJPEG) {
		tr.screenshotJPEG = qfalse;
		RE_TakeScreenshotJPEG(tr.screenshotJPEGName, tr.screenshotJPEGQuality, tr.screenshotJPEGSilent);
	}

	if (tr.levelshot) {
		tr.levelshot = qfalse;
		RE_TakeLevelshot(tr.levelshotName);
	}

	cmd = (swapBuffersCommand_t *)R_GetCommandBufferReserved( sizeof( *cmd ), 0 );
	if (!cmd) {
		return;
	}

	cmd->commandId = RC_SWAP_BUFFERS;
	R_IssueRenderCommands( qtrue );

	R_InitNextFrame();

	if ( frontEndMsec ) {
		*frontEndMsec = tr.frontEndMsec;
	}
	tr.frontEndMsec = 0;
	if ( backEndMsec ) {
		*backEndMsec = backEnd.pc.msec;
	}
	backEnd.pc.msec = 0;
}

/*
=============
RE_CaptureFrameRAW

Capture current frame buffer as raw BGR data. Returns data size.
=============
*/
int RE_CaptureFrameRaw( byte *buffer, int bufSize, int padding )
{
	readPixelsCommand_t	*cmd;
	int		size;

	if( !tr.registered ) {
		return 0;
	}

	cmd = (readPixelsCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if( !cmd ) {
		return 0;
	}

	size = PAD(glConfig.vidWidth * 3, padding) * glConfig.vidHeight;

	// Get raw pixels from backend
	cmd->commandId = RC_READ_PIXELS;

	cmd->buffer = buffer;
	cmd->bufSize = bufSize;
	cmd->padding = padding;
	cmd->format = GL_BGR;

	R_SyncRenderThread();
	//

	if (r_gammamethod->integer == GAMMA_HARDWARE)
		R_GammaCorrect(buffer, size);

	return size;
}

/*
=============
RE_CaptureFrameJPEG

Capture current frame buffer as JPEG image. Returns JPEG size.
=============
*/
int RE_CaptureFrameJPEG( byte *buffer, int bufSize, int quality )
{
	readPixelsCommand_t	*cmd;
	byte	*captureBuffer;
	int		width, height;
	size_t	memcount;
	int		size;

	if( !tr.registered ) {
		return 0;
	}

	cmd = (readPixelsCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if( !cmd ) {
		return 0;
	}

	width = glConfig.vidWidth;
	height = glConfig.vidHeight;
	memcount = width * height * 3;

	captureBuffer = (byte *)ri.Hunk_AllocateTempMemory(memcount);

	// Get raw pixels from backend
	cmd->commandId = RC_READ_PIXELS;

	cmd->buffer = captureBuffer;
	cmd->bufSize = memcount;
	cmd->padding = 1;
	cmd->format = GL_RGB;

	R_SyncRenderThread();
	//

	if (r_gammamethod->integer == GAMMA_HARDWARE)
		R_GammaCorrect(captureBuffer, memcount);

	size = SaveJPGToBuffer(buffer, bufSize,
		quality, width, height, captureBuffer, 0);

	ri.Hunk_FreeTempMemory(captureBuffer);

	return size;
}

/*
=============
RE_RenderWorldEffects
=============
*/
void RE_RenderWorldEffects( void )
{
	worldEffectsCommand_t	*cmd;

	if( !tr.registered ) {
		return;
	}

	cmd = (worldEffectsCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if( !cmd ) {
		return;
	}

	cmd->commandId = RC_WORLD_EFFECTS;
}

/*
=============
RE_GammaCorrection
=============
*/
void RE_GammaCorrection( void )
{
	gammaCorrectionCommand_t	*cmd;

	if( !tr.registered ) {
		return;
	}

	cmd = (gammaCorrectionCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if( !cmd ) {
		return;
	}

	cmd->commandId = RC_GAMMA_CORRECTION;
}

/*
=============
RE_TakeScreenshotJPEG
=============
*/
void RE_TakeScreenshotJPEG( const char *filename, int quality, qboolean silent )
{
	int		width = glConfig.vidWidth;
	int		height = glConfig.vidHeight;
	byte	*buffer;
	int		bufSize;
	int		size;

	bufSize = width * height * 3;
	buffer = (byte *)ri.Hunk_AllocateTempMemory(bufSize);

	size = RE_CaptureFrameJPEG(buffer, bufSize, quality);
	ri.FS_WriteFile(filename, buffer, size);

	if ( !silent ) {
		ri.Printf (PRINT_ALL, "Wrote %s\n", filename);
	}

	ri.Hunk_FreeTempMemory(buffer);
}

/*
=============
RE_TakeScreenshotTGA
=============
*/
void RE_TakeScreenshotTGA( const char *filename, qboolean silent )
{
	int		width, height;
	byte	*buffer;
	byte	*captureBuffer;
	int		bufSize;
	int		captureBufSize;

	width = glConfig.vidWidth;
	height = glConfig.vidHeight;

	captureBufSize = width * height * 3;
	bufSize = captureBufSize + 18;

	buffer = (byte *)ri.Hunk_AllocateTempMemory(bufSize);

	Com_Memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width & 255;
	buffer[13] = width >> 8;
	buffer[14] = height & 255;
	buffer[15] = height >> 8;
	buffer[16] = 24;	// pixel size

	captureBuffer = buffer + 18;

	RE_CaptureFrameRaw(captureBuffer, captureBufSize, 1);
	ri.FS_WriteFile(filename, buffer, bufSize);

	if (!silent) {
		ri.Printf (PRINT_ALL, "Wrote %s\n", filename);
	}

	ri.Hunk_FreeTempMemory(buffer);
}

/*
=============
RE_TakeLevelshot
=============
*/
void RE_TakeLevelshot( const char *filename )
{
	int		width = glConfig.vidWidth;
	int		height = glConfig.vidHeight;
	byte	*captureBuffer;
	byte	*buffer;
	int		captureBufferSize;
	int		bufferSize;
	byte	*src;
	byte	*dst;
	int		x, y;
	int		r, g, b;
	float	xScale, yScale;
	int		xx, yy;

	const int LEVELSHOTSIZE = 256;

	captureBufferSize = width * height * 3;
	captureBuffer = (byte *)ri.Hunk_AllocateTempMemory(captureBufferSize);
	bufferSize = LEVELSHOTSIZE * LEVELSHOTSIZE*3 + 18;
	buffer = (byte *)ri.Hunk_AllocateTempMemory(bufferSize);

	RE_CaptureFrameRaw(captureBuffer, captureBufferSize, 1);

	Com_Memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = LEVELSHOTSIZE & 255;
	buffer[13] = LEVELSHOTSIZE >> 8;
	buffer[14] = LEVELSHOTSIZE & 255;
	buffer[15] = LEVELSHOTSIZE >> 8;
	buffer[16] = 24;	// pixel size

	// resample from source
	xScale = width / (4.0*LEVELSHOTSIZE);
	yScale = height / (3.0*LEVELSHOTSIZE);
	for ( y = 0 ; y < LEVELSHOTSIZE ; y++ ) {
		for ( x = 0 ; x < LEVELSHOTSIZE ; x++ ) {
			r = g = b = 0;
			for ( yy = 0 ; yy < 3 ; yy++ ) {
				for ( xx = 0 ; xx < 4 ; xx++ ) {
					src = captureBuffer + 3 * ( width * (int)( (y*3+yy)*yScale ) + (int)( (x*4+xx)*xScale ) );
					b += src[0];
					g += src[1];
					r += src[2];
				}
			}
			dst = buffer + 18 + 3 * ( y * LEVELSHOTSIZE + x );
			dst[0] = b / 12;
			dst[1] = g / 12;
			dst[2] = r / 12;
		}
	}

	ri.FS_WriteFile(filename, buffer, bufferSize );

	ri.Printf( PRINT_ALL, "Wrote %s\n", filename );

	ri.Hunk_FreeTempMemory( buffer );
	ri.Hunk_FreeTempMemory( captureBuffer );
}

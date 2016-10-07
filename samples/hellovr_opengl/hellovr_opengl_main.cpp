//========= Copyright Valve Corporation ============//

#include <SDL.h>
#include <GL/glew.h>
#include <SDL_opengl.h>
#include <gl/glu.h>
#include <windows.h>
#include <stdio.h>
#include <string>
#include <cstdlib>

#include <openvr.h>

#include "shared/lodepng.h"
#include "shared/Matrices.h"
#include "shared/pathtools.h"

#include <algorithm>
#include <vector>

using std::vector;

#if defined(POSIX)
#include "unistd.h"
#endif

void ThreadSleep( unsigned long nMilliseconds )
{
#if defined(_WIN32)
	::Sleep( nMilliseconds );
#elif defined(POSIX)
	usleep( nMilliseconds * 1000 );
#endif
}

struct TestVertex {
	float aPosition[4];
	float aTexCoord[2];
	uint16_t aThirdAttribute;
	uint16_t pad1;
};

class CGLRenderModel
{
public:
	CGLRenderModel( const std::string & sRenderModelName );
	~CGLRenderModel();

	bool BInit( const vr::RenderModel_t & vrModel, const vr::RenderModel_TextureMap_t & vrDiffuseTexture );
	bool BInit(const char* file_path);
	void Cleanup();
	void Draw();
	const std::string & GetName() const { return m_sModelName; }

private:
	static const size_t kNumVAOs = 1;

	void BInitInternal(const vector<vr::RenderModel_Vertex_t>& vertices, const vector<uint32_t>& indices, size_t i);

	GLuint m_glVertBuffer[kNumVAOs];
	GLuint m_glIndexBuffer[kNumVAOs];
	GLuint m_glVertArray[kNumVAOs];
	GLuint m_glTexture;
	GLsizei m_unVertexCount;
	std::string m_sModelName;
	size_t current_index_;
};

static bool g_bPrintf = true;
static bool g_bUseWorkAround = false;

//-----------------------------------------------------------------------------
// Purpose:
//------------------------------------------------------------------------------
class CMainApplication
{
public:
	CMainApplication( int argc, char *argv[] );
	virtual ~CMainApplication();

	bool BInit();
	bool BInitGL();

	void SetupRenderModels();

	void Shutdown();

	void RunMainLoop();
	bool HandleInput();
	void RenderFrame();

	bool SetupStereoRenderTargets();
	void SetupCameras();

	void RenderStereoTargets();
	void RenderScene( vr::Hmd_Eye nEye );

	Matrix4 GetHMDMatrixProjectionEye( vr::Hmd_Eye nEye );
	Matrix4 GetHMDMatrixPoseEye( vr::Hmd_Eye nEye );
	Matrix4 GetCurrentViewProjectionMatrix( vr::Hmd_Eye nEye );
	void UpdateHMDMatrixPose();

	Matrix4 ConvertSteamVRMatrixToMatrix4( const vr::HmdMatrix34_t &matPose );

	GLuint CompileGLShader( const char *pchShaderName, const char *pchVertexShader, const char *pchFragmentShader );
	bool CreateAllShaders();

	void SetupRenderModelForTrackedDevice( vr::TrackedDeviceIndex_t unTrackedDeviceIndex );
	CGLRenderModel *FindOrLoadRenderModel( const char *pchRenderModelName );

private: 
	bool m_bDebugOpenGL;
	bool m_bVerbose;
	bool m_bPerf;
	bool m_bVblank;
	bool m_bGlFinishHack;

	Matrix4 m_rmat4DevicePose[ vr::k_unMaxTrackedDeviceCount ];
	bool m_rbShowTrackedDevice[ vr::k_unMaxTrackedDeviceCount ];

private: // SDL bookkeeping
	SDL_Window *m_pWindow;
	uint32_t m_nWindowWidth;
	uint32_t m_nWindowHeight;

	SDL_GLContext m_pContext;

private: // OpenGL bookkeeping
	int m_iTrackedControllerCount;
	int m_iTrackedControllerCount_Last;
	int m_iValidPoseCount;
	int m_iValidPoseCount_Last;

	std::string m_strPoseClasses;                            // what classes we saw poses for this frame
	char m_rDevClassChar[ vr::k_unMaxTrackedDeviceCount ];   // for each device, a character representing its class
	
	float m_fNearClip;
	float m_fFarClip;

	GLuint m_glControllerVertBuffer;
	GLuint m_unControllerVAO;
	unsigned int m_uiControllerVertcount;

	Matrix4 m_mat4HMDPose;
	Matrix4 m_mat4eyePosLeft;
	Matrix4 m_mat4eyePosRight;

	Matrix4 m_mat4ProjectionCenter;
	Matrix4 m_mat4ProjectionLeft;
	Matrix4 m_mat4ProjectionRight;

	struct VertexDataScene
	{
		Vector3 position;
		Vector2 texCoord;
	};

	struct VertexDataLens
	{
		Vector2 position;
		Vector2 texCoordRed;
		Vector2 texCoordGreen;
		Vector2 texCoordBlue;
	};

	GLuint m_unControllerTransformProgramID;
	GLuint m_unRenderModelProgramID;

	GLint m_nControllerMatrixLocation;
	GLint m_nRenderModelMatrixLocation;

	struct FramebufferDesc
	{
		GLuint m_nDepthBufferId;
		GLuint m_nRenderTextureId;
		GLuint m_nRenderFramebufferId;
		GLuint m_nResolveTextureId;
		GLuint m_nResolveFramebufferId;
	};
	FramebufferDesc leftEyeDesc;
	FramebufferDesc rightEyeDesc;

	bool CreateFrameBuffer( int nWidth, int nHeight, FramebufferDesc &framebufferDesc );
	
	uint32_t m_nRenderWidth;
	uint32_t m_nRenderHeight;

	CGLRenderModel *m_rTrackedDeviceToRenderModel[ vr::k_unMaxTrackedDeviceCount ];
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void dprintf( const char *fmt, ... )
{
	va_list args;
	char buffer[ 2048 ];

	va_start( args, fmt );
	vsprintf_s( buffer, fmt, args );
	va_end( args );

	if ( g_bPrintf )
		printf( "%s", buffer );

	OutputDebugStringA( buffer );
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CMainApplication::CMainApplication( int argc, char *argv[] )
	: m_pWindow(NULL)
	, m_pContext(NULL)
	, m_nWindowWidth( 1280 )
	, m_nWindowHeight( 720 )
	, m_unControllerTransformProgramID( 0 )
	, m_unRenderModelProgramID( 0 )
	, m_bDebugOpenGL( false )
	, m_bVerbose( false )
	, m_bPerf( false )
	, m_bVblank( false )
	, m_bGlFinishHack( true )
	, m_glControllerVertBuffer( 0 )
	, m_unControllerVAO( 0 )
	, m_nControllerMatrixLocation( -1 )
	, m_nRenderModelMatrixLocation( -1 )
	, m_iTrackedControllerCount( 0 )
	, m_iTrackedControllerCount_Last( -1 )
	, m_iValidPoseCount( 0 )
	, m_iValidPoseCount_Last( -1 )
	, m_strPoseClasses("")
{

	for( int i = 1; i < argc; i++ )
	{
		if( !stricmp( argv[i], "-gldebug" ) )
		{
			m_bDebugOpenGL = true;
		}
		else if( !stricmp( argv[i], "-verbose" ) )
		{
			m_bVerbose = true;
		}
		else if( !stricmp( argv[i], "-novblank" ) )
		{
			m_bVblank = false;
		}
		else if( !stricmp( argv[i], "-noglfinishhack" ) )
		{
			m_bGlFinishHack = false;
		}
		else if( !stricmp( argv[i], "-noprintf" ) )
		{
			g_bPrintf = false;
		}
	}
	// other initialization tasks are done in BInit
	memset(m_rDevClassChar, 0, sizeof(m_rDevClassChar));
	memset(m_rTrackedDeviceToRenderModel, 0, sizeof(m_rTrackedDeviceToRenderModel));
};


//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CMainApplication::~CMainApplication()
{
	// work is done in Shutdown
	dprintf( "Shutdown" );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainApplication::BInit()
{
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER ) < 0 )
	{
		printf("%s - SDL could not initialize! SDL Error: %s\n", __FUNCTION__, SDL_GetError());
		return false;
	}

	int nWindowPosX = 700;
	int nWindowPosY = 100;
	m_nWindowWidth = 1280;
	m_nWindowHeight = 720;
	Uint32 unWindowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;

	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 4 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 1 );
	//SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );

	SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 0 );
	SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 0 );
	if( m_bDebugOpenGL )
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG );

	m_pWindow = SDL_CreateWindow( "hellovr_sdl", nWindowPosX, nWindowPosY, m_nWindowWidth, m_nWindowHeight, unWindowFlags );
	if (m_pWindow == NULL)
	{
		printf( "%s - Window could not be created! SDL Error: %s\n", __FUNCTION__, SDL_GetError() );
		return false;
	}

	m_pContext = SDL_GL_CreateContext(m_pWindow);
	if (m_pContext == NULL)
	{
		printf( "%s - OpenGL context could not be created! SDL Error: %s\n", __FUNCTION__, SDL_GetError() );
		return false;
	}

	glewExperimental = GL_TRUE;
	GLenum nGlewError = glewInit();
	if (nGlewError != GLEW_OK)
	{
		printf( "%s - Error initializing GLEW! %s\n", __FUNCTION__, glewGetErrorString( nGlewError ) );
		return false;
	}
	glGetError(); // to clear the error caused deep in GLEW

	if ( SDL_GL_SetSwapInterval( m_bVblank ? 1 : 0 ) < 0 )
	{
		printf( "%s - Warning: Unable to set VSync! SDL Error: %s\n", __FUNCTION__, SDL_GetError() );
		return false;
	}


	SDL_SetWindowTitle( m_pWindow, "hellovr_sdl" );
	
 	m_fNearClip = 0.1f;
 	m_fFarClip = 30.0f;
 
// 		m_MillisecondsTimer.start(1, this);
// 		m_SecondsTimer.start(1000, this);
	
	if (!BInitGL())
	{
		printf("%s - Unable to initialize OpenGL!\n", __FUNCTION__);
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void APIENTRY DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char* message, const void* userParam)
{
	dprintf( "GL Error: %s\n", message );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainApplication::BInitGL()
{
	if( m_bDebugOpenGL )
	{
		glDebugMessageCallback( (GLDEBUGPROC)DebugCallback, nullptr);
		glDebugMessageControl( GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE );
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	}

	if( !CreateAllShaders() )
		return false;

	SetupCameras();
	SetupStereoRenderTargets();

	SetupRenderModels();

	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::Shutdown()
{
	for (uint32_t unTrackedDevice = 0; unTrackedDevice < vr::k_unMaxTrackedDeviceCount; unTrackedDevice++) {
		if (m_rTrackedDeviceToRenderModel[unTrackedDevice]) {
			delete m_rTrackedDeviceToRenderModel[unTrackedDevice];
		}
		m_rTrackedDeviceToRenderModel[unTrackedDevice] = nullptr;
	}
	
	if( m_pContext )
	{
		glDebugMessageControl( GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_FALSE );
		glDebugMessageCallback(nullptr, nullptr);

		if ( m_unControllerTransformProgramID )
		{
			glDeleteProgram( m_unControllerTransformProgramID );
		}
		if ( m_unRenderModelProgramID )
		{
			glDeleteProgram( m_unRenderModelProgramID );
		}

		glDeleteRenderbuffers( 1, &leftEyeDesc.m_nDepthBufferId );
		glDeleteTextures( 1, &leftEyeDesc.m_nRenderTextureId );
		glDeleteFramebuffers( 1, &leftEyeDesc.m_nRenderFramebufferId );
		glDeleteTextures( 1, &leftEyeDesc.m_nResolveTextureId );
		glDeleteFramebuffers( 1, &leftEyeDesc.m_nResolveFramebufferId );

		glDeleteRenderbuffers( 1, &rightEyeDesc.m_nDepthBufferId );
		glDeleteTextures( 1, &rightEyeDesc.m_nRenderTextureId );
		glDeleteFramebuffers( 1, &rightEyeDesc.m_nRenderFramebufferId );
		glDeleteTextures( 1, &rightEyeDesc.m_nResolveTextureId );
		glDeleteFramebuffers( 1, &rightEyeDesc.m_nResolveFramebufferId );

		if( m_unControllerVAO != 0 )
		{
			glDeleteVertexArrays( 1, &m_unControllerVAO );
		}
	}

	if( m_pWindow )
	{
		SDL_DestroyWindow(m_pWindow);
		m_pWindow = NULL;
	}

	SDL_Quit();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainApplication::HandleInput()
{
	SDL_Event sdlEvent;
	bool bRet = false;

	while ( SDL_PollEvent( &sdlEvent ) != 0 )
	{
		if ( sdlEvent.type == SDL_QUIT )
		{
			bRet = true;
		}
		else if ( sdlEvent.type == SDL_KEYDOWN )
		{
			if ( sdlEvent.key.keysym.sym == SDLK_ESCAPE 
			     || sdlEvent.key.keysym.sym == SDLK_q )
			{
				bRet = true;
			}
		}
		else if (sdlEvent.type == SDL_KEYUP)
		{
			if (sdlEvent.key.keysym.sym == SDLK_r)
			{
				g_bUseWorkAround = !g_bUseWorkAround;
				SetupRenderModels();
			}
		}
	}

	return bRet;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::RunMainLoop()
{
	bool bQuit = false;

	SDL_StartTextInput();
	SDL_ShowCursor( SDL_DISABLE );

	while ( !bQuit )
	{
		bQuit = HandleInput();

		RenderFrame();
	}

	SDL_StopTextInput();
}


double GetTimestampInSeconds() {
  LARGE_INTEGER li_freq;
  QueryPerformanceFrequency(&li_freq);
  LARGE_INTEGER pc;
  QueryPerformanceCounter(&pc);
  return static_cast<double>(pc.QuadPart) / li_freq.QuadPart;
}

void SleepNMilliseconds(double n) {
  const double start = GetTimestampInSeconds();
  while (GetTimestampInSeconds() - start < n * 0.001);
}

static const double kAppStartTimeInSeconds = GetTimestampInSeconds();

class ScopedTimer {
 public:
  ScopedTimer(const char* name)
    : start_time_(GetTimestampInSeconds()), name_(name) {
  }
  ~ScopedTimer() {
    const double now = GetTimestampInSeconds();
    const double duration_in_us = (now - start_time_) * 1000000.0;
	dprintf("%s: %.2f us\n", name_, duration_in_us);
  }

 private:
  double start_time_;
  std::string name_;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::RenderFrame()
{
	// for now as fast as possible
	RenderStereoTargets();

	if ( m_bVblank && m_bGlFinishHack )
	{
		//$ HACKHACK. From gpuview profiling, it looks like there is a bug where two renders and a present
		// happen right before and after the vsync causing all kinds of jittering issues. This glFinish()
		// appears to clear that up. Temporary fix while I try to get nvidia to investigate this problem.
		// 1/29/2014 mikesart
		glFinish();
	}

	// SwapWindow
	{
		SDL_GL_SwapWindow( m_pWindow );
	}

	// Clear
	{
		// We want to make sure the glFinish waits for the entire present to complete, not just the submission
		// of the command. So, we do a clear here right here so the glFinish will wait fully for the swap.
		glClearColor( 0, 0, 0, 1 );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	// Flush and wait for swap.
	if ( m_bVblank )
	{
		glFlush();
		glFinish();
	}

	// Spew out the controller and pose count whenever they change.
	if ( m_iTrackedControllerCount != m_iTrackedControllerCount_Last || m_iValidPoseCount != m_iValidPoseCount_Last )
	{
		m_iValidPoseCount_Last = m_iValidPoseCount;
		m_iTrackedControllerCount_Last = m_iTrackedControllerCount;
		
		dprintf( "PoseCount:%d(%s) Controllers:%d\n", m_iValidPoseCount, m_strPoseClasses.c_str(), m_iTrackedControllerCount );
	}

	UpdateHMDMatrixPose();
}


//-----------------------------------------------------------------------------
// Purpose: Compiles a GL shader program and returns the handle. Returns 0 if
//			the shader couldn't be compiled for some reason.
//-----------------------------------------------------------------------------
GLuint CMainApplication::CompileGLShader( const char *pchShaderName, const char *pchVertexShader, const char *pchFragmentShader )
{
	GLuint unProgramID = glCreateProgram();

	GLuint nSceneVertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource( nSceneVertexShader, 1, &pchVertexShader, NULL);
	glCompileShader( nSceneVertexShader );

	GLint vShaderCompiled = GL_FALSE;
	glGetShaderiv( nSceneVertexShader, GL_COMPILE_STATUS, &vShaderCompiled);
	if ( vShaderCompiled != GL_TRUE)
	{
		dprintf("%s - Unable to compile vertex shader %d!\n", pchShaderName, nSceneVertexShader);
		glDeleteProgram( unProgramID );
		glDeleteShader( nSceneVertexShader );
		return 0;
	}
	glAttachShader( unProgramID, nSceneVertexShader);
	glDeleteShader( nSceneVertexShader ); // the program hangs onto this once it's attached

	GLuint  nSceneFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource( nSceneFragmentShader, 1, &pchFragmentShader, NULL);
	glCompileShader( nSceneFragmentShader );

	GLint fShaderCompiled = GL_FALSE;
	glGetShaderiv( nSceneFragmentShader, GL_COMPILE_STATUS, &fShaderCompiled);
	if (fShaderCompiled != GL_TRUE)
	{
		dprintf("%s - Unable to compile fragment shader %d!\n", pchShaderName, nSceneFragmentShader );
		glDeleteProgram( unProgramID );
		glDeleteShader( nSceneFragmentShader );
		return 0;	
	}

	glAttachShader( unProgramID, nSceneFragmentShader );
	glDeleteShader( nSceneFragmentShader ); // the program hangs onto this once it's attached

	glLinkProgram( unProgramID );

	GLint programSuccess = GL_TRUE;
	glGetProgramiv( unProgramID, GL_LINK_STATUS, &programSuccess);
	if ( programSuccess != GL_TRUE )
	{
		dprintf("%s - Error linking program %d!\n", pchShaderName, unProgramID);
		glDeleteProgram( unProgramID );
		return 0;
	}

	glUseProgram( unProgramID );
	glUseProgram( 0 );

	return unProgramID;
}


//-----------------------------------------------------------------------------
// Purpose: Creates all the shaders used by HelloVR SDL
//-----------------------------------------------------------------------------
bool CMainApplication::CreateAllShaders()
{
	m_unControllerTransformProgramID = CompileGLShader(
		"Controller",

		// vertex shader
		"#version 410\n"
		"uniform mat4 matrix;\n"
		"layout(location = 0) in vec4 position;\n"
		"layout(location = 1) in vec3 v3ColorIn;\n"
		"out vec4 v4Color;\n"
		"void main()\n"
		"{\n"
		"	v4Color.xyz = v3ColorIn; v4Color.a = 1.0;\n"
		"	gl_Position = matrix * position;\n"
		"}\n",

		// fragment shader
		"#version 410\n"
		"in vec4 v4Color;\n"
		"out vec4 outputColor;\n"
		"void main()\n"
		"{\n"
		"   outputColor = v4Color;\n"
		"}\n"
		);
	m_nControllerMatrixLocation = glGetUniformLocation( m_unControllerTransformProgramID, "matrix" );
	if( m_nControllerMatrixLocation == -1 )
	{
		dprintf( "Unable to find matrix uniform in controller shader\n" );
		return false;
	}

	m_unRenderModelProgramID = CompileGLShader( 
		"render model",

		// Vertex Shader
		"#version 410\n"
		"uniform mat4 matrix;\n"
		"layout(location = 0) in vec4 aPosition;\n"
		"layout(location = 1) in vec2 aTexCoord;\n"
		"layout(location = 2) in float aThirdAttribute;\n"
		"out vec2 v2TexCoord;\n"
		"void main()\n"
		"{\n"
		"   v2TexCoord = aTexCoord;\n"
		"   gl_Position = matrix * vec4(aPosition.xyz, 1.0);\n"
		"}\n",

		//fragment shader
		"#version 410 core\n"
		"uniform sampler2D diffuse;\n"
		"in vec2 v2TexCoord;\n"
		"out vec4 outputColor;\n"
		"void main()\n"
		"{\n"
		"   outputColor = texture( diffuse, v2TexCoord);\n"
		"}\n"

		);
	m_nRenderModelMatrixLocation = glGetUniformLocation( m_unRenderModelProgramID, "matrix" );
	if( m_nRenderModelMatrixLocation == -1 )
	{
		dprintf( "Unable to find matrix uniform in render model shader\n" );
		return false;
	}

	return m_unControllerTransformProgramID != 0
		&& m_unRenderModelProgramID != 0;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::SetupCameras()
{
	m_mat4ProjectionLeft = GetHMDMatrixProjectionEye( vr::Eye_Left );
	m_mat4ProjectionRight = GetHMDMatrixProjectionEye( vr::Eye_Right );
	m_mat4eyePosLeft = GetHMDMatrixPoseEye( vr::Eye_Left );
	m_mat4eyePosRight = GetHMDMatrixPoseEye( vr::Eye_Right );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainApplication::CreateFrameBuffer( int nWidth, int nHeight, FramebufferDesc &framebufferDesc )
{
	glGenFramebuffers(1, &framebufferDesc.m_nRenderFramebufferId );
	glBindFramebuffer(GL_FRAMEBUFFER, framebufferDesc.m_nRenderFramebufferId);

	glGenRenderbuffers(1, &framebufferDesc.m_nDepthBufferId);
	glBindRenderbuffer(GL_RENDERBUFFER, framebufferDesc.m_nDepthBufferId);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT, nWidth, nHeight );
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,	framebufferDesc.m_nDepthBufferId );

	glGenTextures(1, &framebufferDesc.m_nRenderTextureId );
	glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, framebufferDesc.m_nRenderTextureId );
	glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, nWidth, nHeight, true);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, framebufferDesc.m_nRenderTextureId, 0);

	glGenFramebuffers(1, &framebufferDesc.m_nResolveFramebufferId );
	glBindFramebuffer(GL_FRAMEBUFFER, framebufferDesc.m_nResolveFramebufferId);

	glGenTextures(1, &framebufferDesc.m_nResolveTextureId );
	glBindTexture(GL_TEXTURE_2D, framebufferDesc.m_nResolveTextureId );
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, nWidth, nHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, framebufferDesc.m_nResolveTextureId, 0);

	// check FBO status
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		return false;
	}

	glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainApplication::SetupStereoRenderTargets()
{
	m_nRenderWidth = 1512;
	m_nRenderHeight = 1680;

	CreateFrameBuffer( m_nRenderWidth, m_nRenderHeight, leftEyeDesc );
	CreateFrameBuffer( m_nRenderWidth, m_nRenderHeight, rightEyeDesc );
	
	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::RenderStereoTargets()
{
	glClearColor( 0.15f, 0.15f, 0.18f, 1.0f ); // nice background color, but not black
	glEnable( GL_MULTISAMPLE );

	uint32_t half_width = m_nWindowWidth / 2;

	// Left Eye
	glBindFramebuffer( GL_FRAMEBUFFER, leftEyeDesc.m_nRenderFramebufferId );
 	glViewport(0, 0, half_width, m_nWindowHeight);
 	RenderScene( vr::Eye_Left );
 	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	
	glDisable( GL_MULTISAMPLE );
	 	
 	glBindFramebuffer(GL_READ_FRAMEBUFFER, leftEyeDesc.m_nRenderFramebufferId);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    glBlitFramebuffer( 0, 0, half_width, m_nWindowHeight, 0, 0, half_width, m_nWindowHeight,
		GL_COLOR_BUFFER_BIT,
 		GL_LINEAR );

 	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	glEnable( GL_MULTISAMPLE );

	// Right Eye
	glBindFramebuffer( GL_FRAMEBUFFER, rightEyeDesc.m_nRenderFramebufferId );
 	glViewport(0, 0, half_width, m_nWindowHeight);
 	RenderScene( vr::Eye_Right );
 	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
 	
	glDisable( GL_MULTISAMPLE );

 	glBindFramebuffer(GL_READ_FRAMEBUFFER, rightEyeDesc.m_nRenderFramebufferId);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	
    glBlitFramebuffer(0, 0, half_width, m_nWindowHeight, half_width, 0, 2*half_width, m_nWindowHeight,
		GL_COLOR_BUFFER_BIT,
 		GL_LINEAR  );

 	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::RenderScene( vr::Hmd_Eye nEye )
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);

	// ----- Render Model rendering -----
	glUseProgram( m_unRenderModelProgramID );

	for( uint32_t unTrackedDevice = 0; unTrackedDevice < vr::k_unMaxTrackedDeviceCount; unTrackedDevice++ )
	{
		if( !m_rTrackedDeviceToRenderModel[ unTrackedDevice ] || !m_rbShowTrackedDevice[ unTrackedDevice ] )
			continue;

		const Matrix4 & matDeviceToTracking = m_rmat4DevicePose[ unTrackedDevice ];
		Matrix4 matMVP = GetCurrentViewProjectionMatrix( nEye ) * matDeviceToTracking;
		glUniformMatrix4fv( m_nRenderModelMatrixLocation, 1, GL_FALSE, matMVP.get() );

		m_rTrackedDeviceToRenderModel[ unTrackedDevice ]->Draw();
	}

	glUseProgram( 0 );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Matrix4 CMainApplication::GetHMDMatrixProjectionEye( vr::Hmd_Eye nEye )
{
	if (nEye == vr::Hmd_Eye::Eye_Left) {
		return Matrix4(
			0.757585824f, 0.000000000f, 0.000000000f, 0.000000000f,
			0.000000000f, 0.681940317f, 0.000000000f, 0.000000000f,
			-0.0568149090f, 9.85278675e-05f, -1.00334454f, -1.00000000f,
			0.000000000f, 0.000000000f, -0.100334451f, 0.000000000f);
	}
	else {
		return Matrix4(
			0.758769333f, 0.000000000f, 0.000000000f, 0.000000000f,
			0.000000000f, 0.682856500f, 0.000000000f, 0.000000000f,
			0.0570514202f, -0.00101399445f, -1.00334454f, -1.00000000f,
			0.000000000f, 0.000000000f, -0.100334451f, 0.000000000f);
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Matrix4 CMainApplication::GetHMDMatrixPoseEye( vr::Hmd_Eye nEye )
{
	if (nEye == vr::Hmd_Eye::Eye_Left) {
		return Matrix4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			-0.0311999992f, 0.0f, 0.0149999997f, 1.0f
		).invert();
	}
	else {
		return Matrix4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0311999992f, 0.0f, 0.0149999997f, 1.0f
		).invert();
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Matrix4 CMainApplication::GetCurrentViewProjectionMatrix( vr::Hmd_Eye nEye )
{
	Matrix4 matMVP;
	if( nEye == vr::Eye_Left )
	{
		matMVP = m_mat4ProjectionLeft * m_mat4eyePosLeft * m_mat4HMDPose;
	}
	else if( nEye == vr::Eye_Right )
	{
		matMVP = m_mat4ProjectionRight * m_mat4eyePosRight *  m_mat4HMDPose;
	}

	return matMVP;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::UpdateHMDMatrixPose()
{
	m_iValidPoseCount = 5;
	m_strPoseClasses = "HTTCC";
	m_rmat4DevicePose[0] = Matrix4(0.660372f, 0.005540f, -0.750918f, 0.000000f, 0.124383f, 0.985353f, 0.116655f, 0.000000f, 0.740566f, -0.170437f, 0.650010f, 0.000000f, -0.762210f, 0.816847f, 0.476603f, 1.000000f);
	m_rDevClassChar[0] = 'H';
	m_rmat4DevicePose[1] = Matrix4(-0.719325f, 0.043348f, -0.693320f, 0.000000f, -0.112347f, 0.977653f, 0.177686f, 0.000000f, 0.685529f, 0.205706f, -0.698380f, 0.000000f, 2.403435f, 2.413503f, -1.104088f, 1.000000f);
	m_rDevClassChar[1] = 'T';
	m_rmat4DevicePose[2] = Matrix4(0.996386f, 0.032452f, -0.078497f, 0.000000f, -0.024020f, -0.778762f, -0.626859f, 0.000000f, -0.081473f, 0.626479f, -0.775168f, 0.000000f, -0.018010f, 3.142777f, -0.990557f, 1.000000f);
	m_rDevClassChar[2] = 'T';
	m_rmat4DevicePose[3] = Matrix4(0.544623f, -0.146795f, 0.825734f, 0.000000f, -0.116081f, 0.961893f, 0.247564f, 0.000000f, -0.830609f, -0.230681f, 0.506829f, 0.000000f, -0.935668f, 0.832183f, 0.417553f, 1.000000f);
	m_rDevClassChar[3] = 'C';
	m_rmat4DevicePose[4] = Matrix4(0.869483f, 0.196030f, 0.453399f, 0.000000f, -0.309990f, 0.931179f, 0.191867f, 0.000000f, -0.384584f, -0.307374f, 0.870412f, 0.000000f, -1.001555f, 0.838425f, 0.263718f, 1.000000f);
	m_rDevClassChar[4] = 'C';

	m_mat4HMDPose = m_rmat4DevicePose[0].invert();
}


//-----------------------------------------------------------------------------
// Purpose: Finds a render model we've already loaded or loads a new one
//-----------------------------------------------------------------------------
CGLRenderModel *CMainApplication::FindOrLoadRenderModel( const char *pchRenderModelName )
{
	CGLRenderModel *pRenderModel = new CGLRenderModel(pchRenderModelName);
	const std::string sExecutableDirectory = Path_StripFilename(Path_GetExecutablePath());
	const std::string file_path = Path_MakeAbsolute(std::string("../") + pchRenderModelName + ".model", sExecutableDirectory);
	pRenderModel->BInit(file_path.c_str());
	return pRenderModel;
}


//-----------------------------------------------------------------------------
// Purpose: Create/destroy GL a Render Model for a single tracked device
//-----------------------------------------------------------------------------
void CMainApplication::SetupRenderModelForTrackedDevice( vr::TrackedDeviceIndex_t unTrackedDeviceIndex )
{
	if( unTrackedDeviceIndex >= vr::k_unMaxTrackedDeviceCount )
		return;

	std::string sRenderModelName;
	if (unTrackedDeviceIndex <= 2) {
		sRenderModelName = "lh_basestation_vive";
	}
	else if (unTrackedDeviceIndex <= 4) {
		sRenderModelName = "vr_controller_vive_1_5";
	}
	else {
		return;
	}

	CGLRenderModel *pRenderModel = FindOrLoadRenderModel(sRenderModelName.c_str());
	if( !pRenderModel )
	{
		dprintf( "Unable to load render model for tracked device %d (%s)", unTrackedDeviceIndex, sRenderModelName.c_str() );
	}
	else
	{
		m_rTrackedDeviceToRenderModel[ unTrackedDeviceIndex ] = pRenderModel;
		m_rbShowTrackedDevice[ unTrackedDeviceIndex ] = true;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Create/destroy GL Render Models
//-----------------------------------------------------------------------------
void CMainApplication::SetupRenderModels()
{
	for (uint32_t unTrackedDevice = 0; unTrackedDevice < vr::k_unMaxTrackedDeviceCount; unTrackedDevice++) {
		if (m_rTrackedDeviceToRenderModel[unTrackedDevice]) {
			delete m_rTrackedDeviceToRenderModel[unTrackedDevice];
		}
		m_rTrackedDeviceToRenderModel[unTrackedDevice] = nullptr;
	}

	for( uint32_t unTrackedDevice = vr::k_unTrackedDeviceIndex_Hmd + 1; unTrackedDevice < vr::k_unMaxTrackedDeviceCount; unTrackedDevice++ )
	{
		SetupRenderModelForTrackedDevice( unTrackedDevice );
	}

}


//-----------------------------------------------------------------------------
// Purpose: Converts a SteamVR matrix to our local matrix class
//-----------------------------------------------------------------------------
Matrix4 CMainApplication::ConvertSteamVRMatrixToMatrix4( const vr::HmdMatrix34_t &matPose )
{
	Matrix4 matrixObj(
		matPose.m[0][0], matPose.m[1][0], matPose.m[2][0], 0.0,
		matPose.m[0][1], matPose.m[1][1], matPose.m[2][1], 0.0,
		matPose.m[0][2], matPose.m[1][2], matPose.m[2][2], 0.0,
		matPose.m[0][3], matPose.m[1][3], matPose.m[2][3], 1.0f
		);
	return matrixObj;
}


//-----------------------------------------------------------------------------
// Purpose: Create/destroy GL Render Models
//-----------------------------------------------------------------------------
CGLRenderModel::CGLRenderModel( const std::string & sRenderModelName )
	: m_sModelName( sRenderModelName ),
	  current_index_(0)
{
	for (size_t i = 0; i < kNumVAOs; ++i) {
		m_glIndexBuffer[i] = 0;
		m_glVertArray[i] = 0;
		m_glVertBuffer[i] = 0;
	}
	m_glTexture = 0;
}


CGLRenderModel::~CGLRenderModel()
{
	Cleanup();
}


//-----------------------------------------------------------------------------
// Purpose: Allocates and populates the GL resources for a render model
//-----------------------------------------------------------------------------
bool CGLRenderModel::BInit( const vr::RenderModel_t & vrModel, const vr::RenderModel_TextureMap_t & vrDiffuseTexture )
{
	// TODO: Used to serialize vr::RenderModel_t and RenderModel_TextureMap_t.
	FILE* fp = nullptr;
	fopen_s(&fp, m_sModelName.c_str(), "wb");
	fwrite(&vrModel.unVertexCount, sizeof(uint32_t), 1, fp);
	fwrite(vrModel.rVertexData, sizeof(vr::RenderModel_Vertex_t), vrModel.unVertexCount, fp);
	fwrite(&vrModel.unTriangleCount, sizeof(uint32_t), 1, fp);
	fwrite(vrModel.rIndexData, sizeof(uint16_t), vrModel.unTriangleCount*3, fp);
	fwrite(&vrDiffuseTexture.unWidth, sizeof(uint16_t), 1, fp);
	fwrite(&vrDiffuseTexture.unHeight, sizeof(uint16_t), 1, fp);
	fwrite(vrDiffuseTexture.rubTextureMapData, 1, vrDiffuseTexture.unWidth * vrDiffuseTexture.unHeight * 4, fp);
	fclose(fp);

	// Make a 10x larger vertex buffer to allow big index values.
	const size_t num_vertices = vrModel.unVertexCount;
	vector<vr::RenderModel_Vertex_t> vertices(num_vertices * 10);
	for (size_t i = 0; i < num_vertices; ++i) {
		for (size_t j = 0; j < 10; ++j) {
			vertices[i + j * num_vertices] = vrModel.rVertexData[i];
		}
	}

	// Convert RenderModel_t indices to 32-bit integers. Duplicate the indices 4x so we can specify large offset.
	const size_t num_indices = vrModel.unTriangleCount * 3;
	vector<uint32_t> indices(num_indices * 4);
	for (size_t i = 0; i < num_indices; ++i) {
		// Make index reference to the last part of the vertices.
		const uint32_t index = (uint32_t)(vrModel.rIndexData[i] + num_vertices * 9);
		indices[i] = index;
		indices[i + num_indices] = index;
		indices[i + num_indices * 2] = index;
		indices[i + num_indices * 3] = index;
	}

	for (size_t i = 0; i < kNumVAOs; ++i) {
		BInitInternal(vertices, indices, i);
	}

	// create and populate the texture
	glGenTextures(1, &m_glTexture );
	glBindTexture( GL_TEXTURE_2D, m_glTexture );

	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, vrDiffuseTexture.unWidth, vrDiffuseTexture.unHeight,
		0, GL_RGBA, GL_UNSIGNED_BYTE, vrDiffuseTexture.rubTextureMapData );

	// If this renders black ask McJohn what's wrong.
	glGenerateMipmap(GL_TEXTURE_2D);

	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );

	GLfloat fLargest;
	glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargest );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest );

	glBindTexture( GL_TEXTURE_2D, 0 );

	m_unVertexCount = vrModel.unTriangleCount * 3;

	return true;
}

bool CGLRenderModel::BInit(const char* file_path)
{
	FILE* fp = nullptr;
	fopen_s(&fp, file_path, "rb");

	uint32_t num_vertices = 0;
	fread(&num_vertices, sizeof(uint32_t), 1, fp);
	vector<vr::RenderModel_Vertex_t> vertices(num_vertices);
	fread(vertices.data(), sizeof(vr::RenderModel_Vertex_t), num_vertices, fp);
	/*for (size_t i = 0; i < num_vertices; ++i) {
		for (size_t j = 1; j < 10; ++j) {
			vertices[i + j * num_vertices] = vertices[i];
		}
	}*/

	uint32_t num_triangles = 0;
	fread(&num_triangles, sizeof(uint32_t), 1, fp);
	const uint32_t num_indices = num_triangles * 3;
	vector<uint16_t> indices16(num_indices);
	fread(indices16.data(), sizeof(uint16_t), num_indices, fp);
	// Convert indices to 32-bit integers.
	vector<uint32_t> indices(num_indices);
	for (size_t i = 0; i < num_indices; ++i) {
		// Make index reference to the last part of the vertices.
		//const uint32_t index = (uint32_t)indices16[i] + num_vertices * 9;
		indices[i] = (uint32_t)indices16[i];
		/*indices[i + num_indices] = index;
		indices[i + num_indices * 2] = index;
		indices[i + num_indices * 3] = index;*/
	}

	for (size_t i = 0; i < kNumVAOs; ++i) {
		BInitInternal(vertices, indices, i);
	}

	uint16_t texture_width = 0;
	uint16_t texture_height = 0;
	fread(&texture_width, sizeof(uint16_t), 1, fp);
	fread(&texture_height, sizeof(uint16_t), 1, fp);
	vector<uint8_t> texture_data(texture_width * texture_height * 4);
	fread(texture_data.data(), 1, texture_data.size(), fp);

	// create and populate the texture
	glGenTextures(1, &m_glTexture);
	glBindTexture(GL_TEXTURE_2D, m_glTexture);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_width, texture_height,
		0, GL_RGBA, GL_UNSIGNED_BYTE, texture_data.data());

	// If this renders black ask McJohn what's wrong.
	glGenerateMipmap(GL_TEXTURE_2D);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

	GLfloat fLargest;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargest);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest);

	glBindTexture(GL_TEXTURE_2D, 0);

	m_unVertexCount = num_indices;

	fclose(fp);

	return true;
}

// [-1, 1] -> [0, 255].
uint8_t Uint8FromFloat(float v) {
	v = std::min(1.0f, std::max(-1.0f, v));
	return (uint8_t)std::min(255, std::max(0, (int32_t)((v + 1.0f) * 255.0f * 0.5f)));
}

// [0, 1] -> [0, 65535].
uint16_t Uint16FromFloat(float v) {
	v = std::min(1.0f, std::max(0.0f, v));
	return (uint16_t)std::min(65535, std::max(0, (int32_t)(v * 65535.0f)));
}

void CGLRenderModel::BInitInternal(const vector<vr::RenderModel_Vertex_t>& vertices, const vector<uint32_t>& indices, size_t i)
{
	// create and bind a VAO to hold state for this model
	glGenVertexArrays(1, &m_glVertArray[i]);
	glBindVertexArray(m_glVertArray[i]);

	// Populate a vertex buffer
	glGenBuffers(1, &m_glVertBuffer[i]);
	glBindBuffer(GL_ARRAY_BUFFER, m_glVertBuffer[i]);
	const size_t vbo_size_in_bytes = 40740000U;
	glBufferData(GL_ARRAY_BUFFER, vbo_size_in_bytes, nullptr, GL_DYNAMIC_DRAW);
	// Convert |vertices| data (in vr::RenderModel_Vertex_t) into TerrainAggVertex, and
	// duplicate it to fully fill the VBO.
	using Vertex = TestVertex;
	{
		vector<Vertex> agg_vertices(vertices.size());
		for (size_t i = 0; i < vertices.size(); ++i) {
			//agg_vertices[i].aPosition[0] = Uint8FromFloat(vertices[i].vPosition.v[0]);
			//agg_vertices[i].aPosition[1] = Uint8FromFloat(vertices[i].vPosition.v[1]);
			//agg_vertices[i].aPosition[2] = Uint8FromFloat(vertices[i].vPosition.v[2]);
			agg_vertices[i].aPosition[0] = vertices[i].vPosition.v[0];
			agg_vertices[i].aPosition[1] = vertices[i].vPosition.v[1];
			agg_vertices[i].aPosition[2] = vertices[i].vPosition.v[2];
			//agg_vertices[i].aTexCoord[0] = Uint16FromFloat(vertices[i].rfTextureCoord[0]);
			//agg_vertices[i].aTexCoord[1] = Uint16FromFloat(vertices[i].rfTextureCoord[1]);
			agg_vertices[i].aTexCoord[0] = vertices[i].rfTextureCoord[0];
			agg_vertices[i].aTexCoord[1] = vertices[i].rfTextureCoord[1];
			agg_vertices[i].aThirdAttribute = 0;
		}

		size_t offset = 0;
		const size_t data_size_in_bytes = sizeof(Vertex) * agg_vertices.size();
		while (offset + data_size_in_bytes <= vbo_size_in_bytes) {
			glBufferSubData(GL_ARRAY_BUFFER, offset, data_size_in_bytes, agg_vertices.data());
			offset += data_size_in_bytes;
		}
	}

	// Identify the components in the vertex buffer
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void *)offsetof(Vertex, aPosition));
	glVertexAttribDivisor(0, 0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void *)offsetof(Vertex, aTexCoord));
	glVertexAttribDivisor(1, 0);
	glEnableVertexAttribArray(1);
	// TODO: If we only specify one short, AMD driver seems to repack the buffer (slow). Instead, pretend it to be 2-element long.
	glVertexAttribPointer(2, g_bUseWorkAround ? 2 : 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(Vertex), (const void *)offsetof(Vertex, aThirdAttribute));
	glVertexAttribDivisor(2, 0);
	glEnableVertexAttribArray(2);

	// Create and populate the index buffer
	glGenBuffers(1, &m_glIndexBuffer[i]);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_glIndexBuffer[i]);
	const size_t index_buffer_size_in_bytes = 19012000U;
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_buffer_size_in_bytes, nullptr, GL_DYNAMIC_DRAW);
	// Duplicate |indices| data to fully fill the VBO.
	{
		size_t offset = 0;
		const size_t data_size_in_bytes = sizeof(uint32_t) * indices.size();
		size_t vertex_offset = 0;
		while (offset + data_size_in_bytes <= index_buffer_size_in_bytes) {
			vector<uint32_t> offseted_indices = indices;
			for (auto& index : offseted_indices) {
				index += (uint32_t)vertex_offset;
			}
			glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset, data_size_in_bytes, offseted_indices.data());
			offset += data_size_in_bytes;
			// 3 times number of vertices, so indices will reference into vertices that are deep into the VBO.
			vertex_offset += 3 * vertices.size();
		}
	}

	glBindVertexArray(0);
}

//-----------------------------------------------------------------------------
// Purpose: Frees the GL resources for a render model
//-----------------------------------------------------------------------------
void CGLRenderModel::Cleanup()
{
	if( m_glVertBuffer )
	{
		glDeleteBuffers(kNumVAOs, m_glIndexBuffer);
		glDeleteVertexArrays(kNumVAOs, m_glVertArray);
		glDeleteBuffers(kNumVAOs, m_glVertBuffer);
		for (size_t i = 0; i < kNumVAOs; ++i) {
			m_glIndexBuffer[i] = 0;
			m_glVertArray[i] = 0;
			m_glVertBuffer[i] = 0;
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Draws the render model
//-----------------------------------------------------------------------------
void CGLRenderModel::Draw()
{
	for (size_t i = 0; i < kNumVAOs; ++i) {
		glBindVertexArray(m_glVertArray[i]);
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, m_glTexture );
		{
			ScopedTimer timer("glDrawElements");
			glDrawElements(GL_TRIANGLES, 262971, GL_UNSIGNED_INT, nullptr);
		}
	}
	glBindVertexArray(0);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	CMainApplication *pMainApplication = new CMainApplication( argc, argv );

	if (!pMainApplication->BInit())
	{
		pMainApplication->Shutdown();
		return 1;
	}

	pMainApplication->RunMainLoop();

	pMainApplication->Shutdown();

	return 0;
}

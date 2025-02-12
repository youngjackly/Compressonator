//=====================================================================
// Copyright (c) 2016-2018    Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
/// \file GPU_OpenGL.cpp
//
//=====================================================================

#include "GPU_OpenGL.h"
#include "MIPS.h"
#include <stdio.h>
#include "GPU_Decode.h"
#include <GLFW/glfw3.h>
#include "GLProgram.h"

#ifdef _WIN32
#pragma comment(lib, "opengl32.lib")        // Open GL
#pragma comment(lib, "Glu32.lib")           // Glu 
#pragma comment(lib, "glew32.lib")          // glew 1.13.0
#else
#include <unistd.h>
#endif
using namespace GPU_Decode;

GLFWwindow* window = NULL;



GPU_OpenGL::GPU_OpenGL(CMP_DWORD Width, CMP_DWORD Height, WNDPROC callback):RenderWindow("OpenGL")
{
    //set default width and height if is 0
    if (Width <= 0)
        Width = 640;
    if (Height <= 0)
        Height = 480;

	if (0 != (InitWindow(Width, Height)))
    {
        fprintf(stderr, "Failed to initialize Window. Please make sure GLEW is downloaded.\n");
        assert(0);
    }



	if(!glfwInit())
		assert(false);
	window = glfwCreateWindow(Width, Height, "OpenGL", NULL, NULL);

	if(!window)
	{
		glfwTerminate();
		assert(false);
	}

	glfwMakeContextCurrent(window);


	// Initialize GLEW
	glewExperimental = GL_TRUE; // Needed for core profile
	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Failed to initialize GLEW. Please make sure GLEW is downloaded.\n");
		assert(false);
	}

	_program = new GLProgram();

	struct PosCoord
	{
		float d[4];
	};

	const PosCoord vetex[] =
	{
		{-1.f, -1.f, 0.f, 0.f},
		{1.f, 1.f, 1.f, 1.f},
		{-1.f, 1.f, 0.f, 1.f},

		{-1.f, -1.f, 0.f, 0.f},
		{1.f, -1.f, 1.f, 0.f},
		{1.f, 1.f, 1.f, 1.f},
	};

	GLuint vertexBuffer;
	glGenBuffers(1, &vertexBuffer );
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof (PosCoord)*6, vetex,  GL_STATIC_DRAW);

	_VBO = vertexBuffer;

	EnableWindowContext();
}

GPU_OpenGL::~GPU_OpenGL()
{
}

//====================================================================================

void GPU_OpenGL::GLRender()
{
    // OpenGL animation code goes here
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

	_program->Use();
    // setup texture mapping
    glEnable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
	glUniform1i(_program->_texLoc, 0);

	glBindBuffer(GL_ARRAY_BUFFER, _VBO);

	glVertexAttribPointer( 0, 4, GL_FLOAT, GL_FALSE, 0, NULL);

	glEnableVertexAttribArray(0);

	 glDrawArrays(GL_TRIANGLES, 0, 6);

#ifdef SHOW_WINDOW
    //for certain image format like bmp, the image texture is upside down, need to use coordinate as below
    //use when showwindow and swapbuffer enabled
    glTexCoord2d(0.0, 1.0); glVertex2d(-1.0, -1.0);
    glTexCoord2d(1.0, 1.0); glVertex2d(+1.0, -1.0);
    glTexCoord2d(1.0, 0.0); glVertex2d(+1.0, +1.0);
    glTexCoord2d(0.0, 0.0); glVertex2d(-1.0, +1.0);
#else
    //for dds use coordinate below 
//    glTexCoord2d(0.0, 0.0); glVertex2d(-1.0, -1.0);
//    glTexCoord2d(1.0, 0.0); glVertex2d(+1.0, -1.0);
//    glTexCoord2d(1.0, 1.0); glVertex2d(+1.0, +1.0);
//    glTexCoord2d(0.0, 1.0); glVertex2d(-1.0, +1.0);
#endif

	glfwSwapBuffers(window);

#ifdef SHOW_WINDOW
    //for debug when showwindow is enable
    SwapBuffers(m_hDC);
#endif

}

GLenum GPU_OpenGL::MIP2OLG_Format(const CMP_Texture* pSourceTexture)
{
    GLenum m_GLnum;
    switch (pSourceTexture->format)
    {
    case CMP_FORMAT_BC1:
    case CMP_FORMAT_DXT1:
        m_GLnum = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        break;
    case CMP_FORMAT_BC2:
    case CMP_FORMAT_DXT3:
        m_GLnum = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        break;
    case CMP_FORMAT_BC3:
    case CMP_FORMAT_DXT5:
        m_GLnum = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        break;
    case CMP_FORMAT_BC4:
    case CMP_FORMAT_ATI1N:
        m_GLnum = GL_COMPRESSED_RED_RGTC1;
        break;
    case CMP_FORMAT_BC5:
    case CMP_FORMAT_ATI2N:
    case CMP_FORMAT_ATI2N_XY:
    case CMP_FORMAT_ATI2N_DXT5:
        m_GLnum = GL_COMPRESSED_RG_RGTC2;
        break;
    case CMP_FORMAT_BC6H:
        m_GLnum = GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT;
        break;
    case CMP_FORMAT_BC6H_SF:
        m_GLnum = GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT;
        break;
    case CMP_FORMAT_BC7:
        m_GLnum = GL_COMPRESSED_RGBA_BPTC_UNORM;
        break;
    case CMP_FORMAT_ETC_RGB:
    case CMP_FORMAT_ETC2_RGB:
        m_GLnum = GL_COMPRESSED_RGB8_ETC2;
        break;
    case CMP_FORMAT_ETC2_SRGB:
        m_GLnum = GL_COMPRESSED_SRGB8_ETC2;
        break;
    case CMP_FORMAT_ETC2_RGBA:
        m_GLnum = GL_COMPRESSED_RGBA8_ETC2_EAC;
        break;
    case CMP_FORMAT_ETC2_RGBA1:
        m_GLnum = GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2;
        break;
    case CMP_FORMAT_ETC2_SRGBA:
        m_GLnum = GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC;
         break;
    case CMP_FORMAT_ETC2_SRGBA1:
        m_GLnum = GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2;
        break;
    case CMP_FORMAT_ASTC:  
        if ((pSourceTexture->nBlockWidth == 4) && (pSourceTexture->nBlockHeight == 4))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
        else if ((pSourceTexture->nBlockWidth == 5) && (pSourceTexture->nBlockHeight == 4))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_5x4_KHR;
        else if ((pSourceTexture->nBlockWidth == 5) && (pSourceTexture->nBlockHeight == 5))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_5x5_KHR;
        else if ((pSourceTexture->nBlockWidth == 6) && (pSourceTexture->nBlockHeight == 5))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_6x5_KHR;
        else if ((pSourceTexture->nBlockWidth == 6) && (pSourceTexture->nBlockHeight == 6))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_6x6_KHR;
        else if ((pSourceTexture->nBlockWidth == 8) && (pSourceTexture->nBlockHeight == 5))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_8x5_KHR;
        else if ((pSourceTexture->nBlockWidth == 8) && (pSourceTexture->nBlockHeight == 6))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_8x6_KHR;
        else if ((pSourceTexture->nBlockWidth == 8) && (pSourceTexture->nBlockHeight == 8))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_8x8_KHR;
        else if ((pSourceTexture->nBlockWidth == 10) && (pSourceTexture->nBlockHeight == 5))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_10x5_KHR;
        else if ((pSourceTexture->nBlockWidth == 10) && (pSourceTexture->nBlockHeight == 6))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_10x6_KHR;
        else if ((pSourceTexture->nBlockWidth == 10) && (pSourceTexture->nBlockHeight == 8))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_10x8_KHR;
        else if ((pSourceTexture->nBlockWidth == 10) && (pSourceTexture->nBlockHeight == 10))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_10x10_KHR;
        else if ((pSourceTexture->nBlockWidth == 12) && (pSourceTexture->nBlockHeight == 10))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_12x10_KHR;
        else if ((pSourceTexture->nBlockWidth == 12) && (pSourceTexture->nBlockHeight == 12))
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_12x12_KHR;
        else 
            m_GLnum = GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
        break;
    default:
        m_GLnum = GL_INVALID_ENUM;
        break;
    }
    return m_GLnum;
}

// load pre-compressed texture
GLuint GPU_OpenGL::LoadTexture(const CMP_Texture* pSourceTexture, bool wrap)
{
    GLenum m_GLnum = MIP2OLG_Format(pSourceTexture);
    if (m_GLnum == GL_INVALID_ENUM)
    {
        fprintf(stderr, "Unsupported format.\n");
        return GLuint(-1);
    }

    glEnable(GL_TEXTURE_2D);

    glGenTextures(1, &texture);

    glBindTexture(GL_TEXTURE_2D, texture);
   
    //modulate to mix texture with color for shading
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // bilinear filter the closest MIP map for small texture
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    // bilinear filter the first MIP map for large texture
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // if wrap is true, the texture wraps over at the edges (repeat)
    // false, the texture ends at the edges (clamp)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,wrap ? GL_REPEAT : GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,wrap ? GL_REPEAT : GL_CLAMP);

    // build texture MIP maps
    glTexParameterf(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);

    //for uncompressed image
    //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);

    //for compressed image (only for mip level 1)
	glCompressedTexImage2D(GL_TEXTURE_2D, 0, m_GLnum, pSourceTexture->dwWidth, pSourceTexture->dwHeight, 0, pSourceTexture->dwDataSize, pSourceTexture->pData);
    
    return texture;
}

// Free Texture
void GPU_OpenGL::FreeTexture(GLuint texture)
{
    glDeleteTextures(1, &texture);
}

//=========================================================================================

CMP_ERROR WINAPI GPU_OpenGL::Decompress(
    const CMP_Texture* pSourceTexture,
    CMP_Texture* pDestTexture
)
{

    GLint majVer = 0;
    GLint minVer = 0;

    glGetIntegerv(GL_MAJOR_VERSION, &majVer);
    glGetIntegerv(GL_MINOR_VERSION, &minVer);

    if (majVer < 3 || (majVer < 3 && minVer < 2))
    {
        PrintInfo("Error: OpenGL 3.2 and up cannot be detected.\n");
        fprintf(stderr, "Error: OpenGL 3.2 and up cannot be detected.\n" );
        return CMP_ERR_UNABLE_TO_INIT_DECOMPRESSLIB;
    }

    texture = LoadTexture(pSourceTexture, false);
    if (texture == -1)
    {
        return CMP_ERR_UNSUPPORTED_SOURCE_FORMAT;
    }

#ifdef SHOW_WINDOW
    //for debug, show window to view image
    ShowWindow(m_hWnd, SW_SHOW);
#endif
    //  Wait in Main message loop, until render is complete
    //  then exit
#ifdef _WIN32
    MSG msg = { 0 };
    int loopcount = 0;
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) && (loopcount < 100))
        {
            loopcount++;
            TranslateMessage(&msg);
        }
        else
        {
            GLRender();
            break;
        }
    }
#else
	GLRender();
#endif

	while (!glfwWindowShouldClose(window)) {

		glfwPollEvents();
		usleep(10000);

	}


    if (pDestTexture)
    {
        if (pSourceTexture->format == CMP_FORMAT_ETC_RGB ||
            pSourceTexture->format == CMP_FORMAT_ETC2_RGB ||
            pSourceTexture->format == CMP_FORMAT_ETC2_RGBA ||
            pSourceTexture->format == CMP_FORMAT_ETC2_RGBA1 
            )
            glReadPixels(0, 0, pDestTexture->dwWidth, pDestTexture->dwHeight, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pDestTexture->pData);
        else
        if (pSourceTexture->format == CMP_FORMAT_ETC2_SRGB ||
            pSourceTexture->format == CMP_FORMAT_ETC2_SRGBA ||
            pSourceTexture->format == CMP_FORMAT_ETC2_SRGBA1
            )
            glReadPixels(0, 0, pDestTexture->dwWidth, pDestTexture->dwHeight, GL_BGRA_EXT, GL_BYTE, pDestTexture->pData);
        else
        {
            if(pDestTexture->format ==  CMP_FORMAT_ARGB_16F)
                glReadPixels(0, 0, pDestTexture->dwWidth, pDestTexture->dwHeight, GL_RGBA, GL_HALF_FLOAT, pDestTexture->pData);
            else
                glReadPixels(0, 0, pDestTexture->dwWidth, pDestTexture->dwHeight, GL_RGBA, GL_UNSIGNED_BYTE, pDestTexture->pData);
        }
    }

	glfwTerminate();

    // free the texture
    FreeTexture(texture);

  

    return CMP_OK; // msg.wParam;
}



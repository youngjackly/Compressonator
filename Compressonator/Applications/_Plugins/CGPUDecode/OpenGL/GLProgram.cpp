//=====================================================================
// Copyright 2019 (c), Advanced Micro Devices, Inc. All rights reserved.
//=====================================================================
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

#include "GLProgram.h"

static const GLchar* vetexCode =
		"#version 310 es\n"
		"layout (location = 0) in vec4 aPosCoord;\n"
		"out vec2 oCoord;\n"
		"void main()\n"
		"{\n"
			"gl_Position = vec4(aPosCoord.x, aPosCoord.y, 0.5, 1.0);\n"
			"oCoord = aPosCoord.zw;\n"
		"};\n";

static const GLchar* fragCode =
		"#version 310 es\n"
		"precision highp float;\n"
		"uniform sampler2D tex;\n"
		"in vec2 oCoord;\n"
		"out vec4 o_fragColor;\n"
		"void main()\n"
		"{\n"
			"o_fragColor = texture(tex, oCoord);\n"
		"}\n";


static const GLchar* frag2 =
		"#version 310 es\n"
		"precision highp float;\n"
		"uniform sampler2D tex;\n"
		"in vec2 oCoord;\n"
		"out vec4 o_fragColor;\n"
		"void main()\n"
		"{\n"
			"vec4 c = texture(tex, oCoord);\n"
			"vec2 v1 = c.xy*2.0 + vec2(-1.0, -1.0);\n"
			"float b = sqrt(clamp(1.0 - dot(v1, v1), 0.0, 1.0));\n"
			"b = (b + 1.0)*0.5;\n"
			"o_fragColor = c;\n"
			"o_fragColor.b = b;\n"
		"}\n";




GLProgram::GLProgram()
{
	this->build(vetexCode, frag2);
	_texLoc = glGetUniformLocation(this->Program, "tex");
	if( 0 != glGetError())
	{
		printf("gl shader not compile right\n");
	}
}

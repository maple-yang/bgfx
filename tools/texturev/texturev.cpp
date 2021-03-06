/*
 * Copyright 2011-2017 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */

#include "common.h"
#include <bgfx/bgfx.h>
#include <bx/commandline.h>
#include <bx/os.h>
#include <bx/string.h>
#include <bx/uint32_t.h>
#include <bx/fpumath.h>
#include <bx/easing.h>
#include <entry/entry.h>
#include <entry/input.h>
#include <entry/cmd.h>
#include <imgui/imgui.h>
#include <bgfx_utils.h>

#include <dirent.h>

#include <bx/crtimpl.h>

#include <tinystl/allocator.h>
#include <tinystl/vector.h>
#include <string>
namespace stl = tinystl;

#include <bimg/decode.h>

#include <bgfx/embedded_shader.h>

#include "vs_texture.bin.h"
#include "vs_texture_cube.bin.h"

#include "fs_texture.bin.h"
#include "fs_texture_array.bin.h"
#include "fs_texture_cube.bin.h"
#include "fs_texture_sdf.bin.h"
#include "fs_texture_3d.bin.h"

#define BACKGROUND_VIEW_ID 0
#define IMAGE_VIEW_ID      1

#define BGFX_TEXTUREV_VERSION_MAJOR 1
#define BGFX_TEXTUREV_VERSION_MINOR 0

static const bgfx::EmbeddedShader s_embeddedShaders[] =
{
	BGFX_EMBEDDED_SHADER(vs_texture),
	BGFX_EMBEDDED_SHADER(fs_texture),
	BGFX_EMBEDDED_SHADER(fs_texture_array),
	BGFX_EMBEDDED_SHADER(vs_texture_cube),
	BGFX_EMBEDDED_SHADER(fs_texture_cube),
	BGFX_EMBEDDED_SHADER(fs_texture_sdf),
	BGFX_EMBEDDED_SHADER(fs_texture_3d),

	BGFX_EMBEDDED_SHADER_END()
};

static const char* s_supportedExt[] =
{
	"bmp",
	"dds",
	"exr",
	"gif",
	"jpg",
	"jpeg",
	"hdr",
	"ktx",
	"png",
	"psd",
	"pvr",
	"tga",
};

struct Binding
{
	enum Enum
	{
		App,
		View,

		Count
	};
};

static const InputBinding s_bindingApp[] =
{
	{ entry::Key::Esc,  entry::Modifier::None,  1, NULL, "exit"                },
	{ entry::Key::KeyQ, entry::Modifier::None,  1, NULL, "exit"                },
	{ entry::Key::KeyF, entry::Modifier::None,  1, NULL, "graphics fullscreen" },

	INPUT_BINDING_END
};

static const InputBinding s_bindingView[] =
{
	{ entry::Key::Comma,     entry::Modifier::None,       1, NULL, "view mip prev"           },
	{ entry::Key::Period,    entry::Modifier::None,       1, NULL, "view mip next"           },
	{ entry::Key::Comma,     entry::Modifier::LeftShift,  1, NULL, "view mip"                },
	{ entry::Key::Comma,     entry::Modifier::RightShift, 1, NULL, "view mip"                },

	{ entry::Key::Slash,     entry::Modifier::None,       1, NULL, "view filter"             },

	{ entry::Key::Key1,      entry::Modifier::None,       1, NULL, "view zoom 1.0\n"
	                                                               "view fit\n"              },

	{ entry::Key::Key0,      entry::Modifier::None,       1, NULL, "view zoom 1.0\n"
	                                                               "view rotate 0\n"
	                                                               "view pan\n"              },
	{ entry::Key::Plus,      entry::Modifier::None,       1, NULL, "view zoom +0.1"          },
	{ entry::Key::Minus,     entry::Modifier::None,       1, NULL, "view zoom -0.1"          },

	{ entry::Key::KeyZ,      entry::Modifier::None,       1, NULL, "view rotate -90"         },
	{ entry::Key::KeyZ,      entry::Modifier::LeftShift,  1, NULL, "view rotate +90"         },

	{ entry::Key::Up,        entry::Modifier::None,       1, NULL, "view file-up"            },
	{ entry::Key::Down,      entry::Modifier::None,       1, NULL, "view file-down"          },
	{ entry::Key::PageUp,    entry::Modifier::None,       1, NULL, "view file-pgup"          },
	{ entry::Key::PageDown,  entry::Modifier::None,       1, NULL, "view file-pgdown"        },

	{ entry::Key::Left,      entry::Modifier::None,       1, NULL, "view layer prev"         },
	{ entry::Key::Right,     entry::Modifier::None,       1, NULL, "view layer next"         },

	{ entry::Key::KeyR,      entry::Modifier::None,       1, NULL, "view rgb r"              },
	{ entry::Key::KeyG,      entry::Modifier::None,       1, NULL, "view rgb g"              },
	{ entry::Key::KeyB,      entry::Modifier::None,       1, NULL, "view rgb b"              },
	{ entry::Key::KeyA,      entry::Modifier::None,       1, NULL, "view rgb a"              },

	{ entry::Key::KeyH,      entry::Modifier::None,       1, NULL, "view help"               },

	{ entry::Key::KeyS,      entry::Modifier::None,       1, NULL, "view sdf"                },

	INPUT_BINDING_END
};

static const char* s_bindingName[] =
{
	"App",
	"View",
};
BX_STATIC_ASSERT(Binding::Count == BX_COUNTOF(s_bindingName) );

static const InputBinding* s_binding[] =
{
	s_bindingApp,
	s_bindingView,
};
BX_STATIC_ASSERT(Binding::Count == BX_COUNTOF(s_binding) );

struct View
{
	View()
		: m_fileIndex(0)
		, m_scaleFn(0)
		, m_mip(0)
		, m_layer(0)
		, m_abgr(UINT32_MAX)
		, m_posx(0.0f)
		, m_posy(0.0f)
		, m_zoom(1.0f)
		, m_angle(0.0f)
		, m_filter(true)
		, m_fit(true)
		, m_alpha(false)
		, m_help(false)
		, m_sdf(false)
	{
	}

	~View()
	{
	}
	int32_t cmd(int32_t _argc, char const* const* _argv)
	{
		if (_argc >= 2)
		{
			if (0 == bx::strCmp(_argv[1], "mip") )
			{
				if (_argc >= 3)
				{
					uint32_t mip = m_mip;
					if (0 == bx::strCmp(_argv[2], "next") )
					{
						++mip;
					}
					else if (0 == bx::strCmp(_argv[2], "prev") )
					{
						--mip;
					}
					else if (0 == bx::strCmp(_argv[2], "last") )
					{
						mip = INT32_MAX;
					}
					else
					{
						mip = atoi(_argv[2]);
					}

					m_mip = bx::uint32_iclamp(mip, 0, m_info.numMips-1);
				}
				else
				{
					m_mip = 0;
				}
			}
			if (0 == bx::strCmp(_argv[1], "layer") )
			{
				if (_argc >= 3)
				{
					uint32_t layer = m_layer;
					if (0 == bx::strCmp(_argv[2], "next") )
					{
						++layer;
					}
					else if (0 == bx::strCmp(_argv[2], "prev") )
					{
						--layer;
					}
					else if (0 == bx::strCmp(_argv[2], "last") )
					{
						layer = INT32_MAX;
					}
					else
					{
						layer = atoi(_argv[2]);
					}

					m_layer = bx::uint32_iclamp(layer, 0, m_info.numLayers-1);
				}
				else
				{
					m_layer = 0;
				}
			}
			else if (0 == bx::strCmp(_argv[1], "pan") )
			{
				if (_argc >= 3)
				{
					if (_argc >= 4)
					{
						float yy;
						bx::fromString(&yy, _argv[3]);
						if (_argv[3][0] == '+'
						||  _argv[3][0] == '-')
						{
							m_posy += yy;
						}
						else
						{
							m_posy = yy;
						}
					}

					float xx;
					bx::fromString(&xx, _argv[2]);
					if (_argv[2][0] == '+'
					||  _argv[2][0] == '-')
					{
						m_posx += xx;
					}
					else
					{
						m_posx = xx;
					}
				}
				else
				{
					m_posx = 0.0f;
					m_posy = 0.0f;
				}
			}
			else if (0 == bx::strCmp(_argv[1], "zoom") )
			{
				if (_argc >= 3)
				{
					float zoom;
					bx::fromString(&zoom, _argv[2]);

					if (_argv[2][0] == '+'
					||  _argv[2][0] == '-')
					{
						m_zoom += zoom;
					}
					else
					{
						m_zoom = zoom;
					}

					m_zoom = bx::fclamp(m_zoom, 0.01f, 10.0f);
				}
				else
				{
					m_zoom = 1.0f;
				}
			}
			else if (0 == bx::strCmp(_argv[1], "rotate") )
			{
				if (_argc >= 3)
				{
					float angle;
					bx::fromString(&angle, _argv[2]);

					if (_argv[2][0] == '+'
					||  _argv[2][0] == '-')
					{
						m_angle += bx::toRad(angle);
					}
					else
					{
						m_angle = bx::toRad(angle);
					}

					m_angle = bx::fwrap(m_angle, bx::kPi*2.0f);
				}
				else
				{
					m_angle = 0.0f;
				}
			}
			else if (0 == bx::strCmp(_argv[1], "filter") )
			{
				if (_argc >= 3)
				{
					m_filter = bx::toBool(_argv[2]);
				}
				else
				{
					m_filter ^= true;
				}
			}
			else if (0 == bx::strCmp(_argv[1], "fit") )
			{
				if (_argc >= 3)
				{
					m_fit = bx::toBool(_argv[2]);
				}
				else
				{
					m_fit ^= true;
				}
			}
			else if (0 == bx::strCmp(_argv[1], "file-up") )
			{
				m_fileIndex = bx::uint32_satsub(m_fileIndex, 1);
			}
			else if (0 == bx::strCmp(_argv[1], "file-down") )
			{
				uint32_t numFiles = bx::uint32_satsub(uint32_t(m_fileList.size() ), 1);
				++m_fileIndex;
				m_fileIndex = bx::uint32_min(m_fileIndex, numFiles);
			}
			else if (0 == bx::strCmp(_argv[1], "rgb") )
			{
				if (_argc >= 3)
				{
					if (_argv[2][0] == 'r')
					{
						m_abgr ^= 0x000000ff;
					}
					else if (_argv[2][0] == 'g')
					{
						m_abgr ^= 0x0000ff00;
					}
					else if (_argv[2][0] == 'b')
					{
						m_abgr ^= 0x00ff0000;
					}
					else if (_argv[2][0] == 'a')
					{
						m_alpha ^= true;
					}
				}
				else
				{
					m_abgr  = UINT32_MAX;
					m_alpha = false;
				}
			}
			else if (0 == bx::strCmp(_argv[1], "sdf") )
			{
				m_sdf ^= true;
			}
			else if (0 == bx::strCmp(_argv[1], "help") )
			{
				m_help ^= true;
			}
		}

		return 0;
	}

	void updateFileList(const char* _path, const char* _fileName = "")
	{
		std::string path = _path;

		DIR* dir = opendir(_path);

		if (NULL == dir)
		{
			path = ".";
		}

		dir = opendir(path.c_str() );
		if (NULL != dir)
		{
			for (dirent* item = readdir(dir); NULL != item; item = readdir(dir) )
			{
				if (0 == (item->d_type & DT_DIR) )
				{
					const char* ext = bx::strRFind(item->d_name, '.');
					if (NULL != ext)
					{
						ext += 1;
						bool supported = false;
						for (uint32_t ii = 0; ii < BX_COUNTOF(s_supportedExt); ++ii)
						{
							if (0 == bx::strCmpI(ext, s_supportedExt[ii]) )
							{
								supported = true;
								break;
							}
						}

						if (supported)
						{
							if (0 == bx::strCmp(_fileName, item->d_name) )
							{
								m_fileIndex = uint32_t(m_fileList.size() );
							}

							std::string name = path;
							char ch = name[name.size()-1];
							name += '/' == ch || '\\' == ch ? "" : "/";
							name += item->d_name;
							m_fileList.push_back(name);
						}
					}
				}
			}

			closedir(dir);
		}
	}

	typedef stl::vector<std::string> FileList;
	FileList m_fileList;

	bgfx::TextureInfo m_info;
	uint32_t m_fileIndex;
	uint32_t m_scaleFn;
	uint32_t m_mip;
	uint32_t m_layer;
	uint32_t m_abgr;
	float    m_posx;
	float    m_posy;
	float    m_zoom;
	float    m_angle;
	bool     m_filter;
	bool     m_fit;
	bool     m_alpha;
	bool     m_help;
	bool     m_sdf;
};

int cmdView(CmdContext* /*_context*/, void* _userData, int _argc, char const* const* _argv)
{
	View* view = static_cast<View*>(_userData);
	return view->cmd(_argc, _argv);
}

struct PosUvColorVertex
{
	float m_x;
	float m_y;
	float m_u;
	float m_v;
	uint32_t m_abgr;

	static void init()
	{
		ms_decl
			.begin()
			.add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
			.end();
	}

	static bgfx::VertexDecl ms_decl;
};

bgfx::VertexDecl PosUvColorVertex::ms_decl;

bool screenQuad(int32_t _x, int32_t _y, int32_t _width, uint32_t _height, uint32_t _abgr, float _maxu = 1.0f, float _maxv = 1.0f)
{
	if (6 == bgfx::getAvailTransientVertexBuffer(6, PosUvColorVertex::ms_decl) )
	{
		bgfx::TransientVertexBuffer vb;
		bgfx::allocTransientVertexBuffer(&vb, 6, PosUvColorVertex::ms_decl);
		PosUvColorVertex* vertex = (PosUvColorVertex*)vb.data;

		const float widthf  = float(_width);
		const float heightf = float(_height);

		const float minx = float(_x);
		const float miny = float(_y);
		const float maxx = minx+widthf;
		const float maxy = miny+heightf;

		const float minu = 0.0f;
		const float maxu = _maxu;
		const float minv = 0.0f;
		const float maxv = _maxv;

		vertex[0].m_x = minx;
		vertex[0].m_y = miny;
		vertex[0].m_u = minu;
		vertex[0].m_v = minv;

		vertex[1].m_x = maxx;
		vertex[1].m_y = miny;
		vertex[1].m_u = maxu;
		vertex[1].m_v = minv;

		vertex[2].m_x = maxx;
		vertex[2].m_y = maxy;
		vertex[2].m_u = maxu;
		vertex[2].m_v = maxv;

		vertex[3].m_x = maxx;
		vertex[3].m_y = maxy;
		vertex[3].m_u = maxu;
		vertex[3].m_v = maxv;

		vertex[4].m_x = minx;
		vertex[4].m_y = maxy;
		vertex[4].m_u = minu;
		vertex[4].m_v = maxv;

		vertex[5].m_x = minx;
		vertex[5].m_y = miny;
		vertex[5].m_u = minu;
		vertex[5].m_v = minv;

		vertex[0].m_abgr = _abgr;
		vertex[1].m_abgr = _abgr;
		vertex[2].m_abgr = _abgr;
		vertex[3].m_abgr = _abgr;
		vertex[4].m_abgr = _abgr;
		vertex[5].m_abgr = _abgr;

		bgfx::setVertexBuffer(0, &vb);

		return true;
	}

	return false;
}

template<bx::LerpFn lerpT, bx::EaseFn easeT>
struct InterpolatorT
{
	float from;
	float to;
	float duration;
	int64_t offset;

	InterpolatorT(float _value)
	{
		reset(_value);
	}

	void reset(float _value)
	{
		from     = _value;
		to       = _value;
		duration = 0.0;
		offset   = bx::getHPCounter();
	}

	void set(float _value, float _duration)
	{
		if (_value != to)
		{
			from     = getValue();
			to       = _value;
			duration = _duration;
			offset   = bx::getHPCounter();
		}
	}

	float getValue()
	{

		if (duration > 0.0)
		{
			const double freq = double(bx::getHPFrequency() );
			int64_t now = bx::getHPCounter();
			float time = (float)(double(now - offset) / freq);
			float lerp = bx::fclamp(time, 0.0, duration) / duration;
			return lerpT(from, to, easeT(lerp) );
		}

		return to;
	}
};

typedef InterpolatorT<bx::flerp,     bx::easeInOutQuad>  Interpolator;
typedef InterpolatorT<bx::angleLerp, bx::easeInOutCubic> InterpolatorAngle;

void keyBindingHelp(const char* _bindings, const char* _description)
{
	ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), _bindings);
	ImGui::SameLine(100);
	ImGui::Text(_description);
}

void associate()
{
#if BX_PLATFORM_WINDOWS
	std::string str;

	char exec[MAX_PATH];
	GetModuleFileNameA(GetModuleHandleA(NULL), exec, MAX_PATH);

	std::string strExec = bx::replaceAll<std::string>(exec, "\\", "\\\\");

	std::string value;
	bx::stringPrintf(value, "@=\"\\\"%s\\\" \\\"%%1\\\"\"\r\n\r\n", strExec.c_str() );

	str += "Windows Registry Editor Version 5.00\r\n\r\n";

	str += "[HKEY_CLASSES_ROOT\\texturev\\shell\\open\\command]\r\n";
	str += value;

	str += "[HKEY_CLASSES_ROOT\\Applications\\texturev.exe\\shell\\open\\command]\r\n";
	str += value;

	for (uint32_t ii = 0; ii < BX_COUNTOF(s_supportedExt); ++ii)
	{
		const char* ext = s_supportedExt[ii];

		bx::stringPrintf(str, "[-HKEY_CLASSES_ROOT\\.%s]\r\n\r\n", ext);
		bx::stringPrintf(str, "[-HKEY_CURRENT_USER\\Software\\Classes\\.%s]\r\n\r\n", ext);

		bx::stringPrintf(str, "[HKEY_CLASSES_ROOT\\.%s]\r\n@=\"texturev\"\r\n\r\n", ext);
		bx::stringPrintf(str, "[HKEY_CURRENT_USER\\Software\\Classes\\.%s]\r\n@=\"texturev\"\r\n\r\n", ext);
	}

	char temp[MAX_PATH];
	GetTempPathA(MAX_PATH, temp);
	bx::strCat(temp, MAX_PATH, "\\texturev.reg");

	bx::FileWriter writer;
	bx::Error err;
	if (bx::open(&writer, temp, false, &err) )
	{
		bx::write(&writer, str.c_str(), uint32_t(str.length()), &err);
		bx::close(&writer);

		if (err.isOk() )
		{
			std::string cmd;
			bx::stringPrintf(cmd, "regedit.exe /s %s", temp);

			bx::ProcessReader reader;
			if (bx::open(&reader, cmd.c_str(), &err) )
			{
				bx::close(&reader);
			}
		}
	}
#elif BX_PLATFORM_LINUX
	std::string str;
	str += "#/bin/bash\n\n";

	for (uint32_t ii = 0; ii < BX_COUNTOF(s_supportedExt); ++ii)
	{
		const char* ext = s_supportedExt[ii];
		bx::stringPrintf(str, "xdg-mime default texturev.desktop image/%s\n", ext);
	}

	str += "\n";

	bx::FileWriter writer;
	bx::Error err;
	if (bx::open(&writer, "/tmp/texturev.sh", false, &err) )
	{
		bx::write(&writer, str.c_str(), uint32_t(str.length()), &err);
		bx::close(&writer);

		if (err.isOk() )
		{
			bx::ProcessReader reader;
			if (bx::open(&reader, "/bin/bash /tmp/texturev.sh", &err) )
			{
				bx::close(&reader);
			}
		}
	}
#endif // BX_PLATFORM_WINDOWS
}

void help(const char* _error = NULL)
{
	if (NULL != _error)
	{
		fprintf(stderr, "Error:\n%s\n\n", _error);
	}

	fprintf(stderr
		, "texturev, bgfx texture viewer tool, version %d.%d.%d.\n"
		  "Copyright 2011-2017 Branimir Karadzic. All rights reserved.\n"
		  "License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause\n\n"
		, BGFX_TEXTUREV_VERSION_MAJOR
		, BGFX_TEXTUREV_VERSION_MINOR
		, BGFX_API_VERSION
		);

	fprintf(stderr
		, "Usage: texturev <file path>\n"
		  "\n"
		  "Supported input file types:\n"
		  );

	for (uint32_t ii = 0; ii < BX_COUNTOF(s_supportedExt); ++ii)
	{
		fprintf(stderr, "    *.%s\n", s_supportedExt[ii]);
	}

	fprintf(stderr
		, "\n"
		  "Options:\n"
		  "  -h, --help               Help.\n"
		  "  -v, --version            Version information only.\n"
		  "      --associate          Associate file extensions with texturev.\n"
		  "\n"
		  "For additional information, see https://github.com/bkaradzic/bgfx\n"
		);
}

int _main_(int _argc, char** _argv)
{
	bx::CommandLine cmdLine(_argc, _argv);

	if (cmdLine.hasArg('v', "version") )
	{
		fprintf(stderr
			, "texturev, bgfx texture viewer tool, version %d.%d.%d.\n"
			, BGFX_TEXTUREV_VERSION_MAJOR
			, BGFX_TEXTUREV_VERSION_MINOR
			, BGFX_API_VERSION
			);
		return bx::kExitSuccess;
	}

	if (cmdLine.hasArg('h', "help") )
	{
		help();
		return bx::kExitFailure;
	}
	else if (cmdLine.hasArg("associate") )
	{
		associate();
		return bx::kExitFailure;
	}

	uint32_t width  = 1280;
	uint32_t height = 720;
	uint32_t debug  = BGFX_DEBUG_TEXT;
	uint32_t reset  = BGFX_RESET_VSYNC;

	inputAddBindings(s_bindingName[Binding::App],  s_binding[Binding::App]);
	inputAddBindings(s_bindingName[Binding::View], s_binding[Binding::View]);

	View view;
	cmdAdd("view", cmdView, &view);

	bgfx::init();
	bgfx::reset(width, height, reset);

	// Set view 0 clear state.
	bgfx::setViewClear(BACKGROUND_VIEW_ID
		, BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH
		, 0x000000ff
		, 1.0f
		, 0
		);

	imguiCreate();

	PosUvColorVertex::init();

	const bgfx::Caps* caps = bgfx::getCaps();
	bgfx::RendererType::Enum type = caps->rendererType;

	bgfx::ShaderHandle vsTexture      = bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_texture");
	bgfx::ShaderHandle fsTexture      = bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_texture");
	bgfx::ShaderHandle fsTextureArray = bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_texture_array");

	bgfx::ProgramHandle textureProgram = bgfx::createProgram(
			  vsTexture
			, fsTexture
			, true
			);
	bgfx::ProgramHandle textureArrayProgram = bgfx::createProgram(
			  vsTexture
			, bgfx::isValid(fsTextureArray)
			? fsTextureArray
			: fsTexture
			, true
			);

	bgfx::ProgramHandle textureCubeProgram = bgfx::createProgram(
			  bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_texture_cube")
			, bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_texture_cube")
			, true
			);

	bgfx::ProgramHandle textureSdfProgram = bgfx::createProgram(
			  vsTexture
			, bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_texture_sdf")
			, true);

	bgfx::ProgramHandle texture3DProgram = bgfx::createProgram(
			  vsTexture
			, bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_texture_3d")
			, true);

	bgfx::UniformHandle s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Int1);
	bgfx::UniformHandle u_mtx      = bgfx::createUniform("u_mtx",      bgfx::UniformType::Mat4);
	bgfx::UniformHandle u_params   = bgfx::createUniform("u_params",   bgfx::UniformType::Vec4);

	const uint32_t checkerBoardSize = 64;
	bgfx::TextureHandle checkerBoard;
	{
		const bgfx::Memory* mem = bgfx::alloc(checkerBoardSize*checkerBoardSize*4);
		bimg::imageCheckerboard(mem->data, checkerBoardSize, checkerBoardSize, 8, 0xff8e8e8e, 0xff5d5d5d);
		checkerBoard = bgfx::createTexture2D(checkerBoardSize, checkerBoardSize, false, 1
			, bgfx::TextureFormat::BGRA8
			, 0
			| BGFX_TEXTURE_MIN_POINT
			| BGFX_TEXTURE_MIP_POINT
			| BGFX_TEXTURE_MAG_POINT
			, mem
			);
	}

	float speed = 0.37f;
	float time  = 0.0f;

	Interpolator mip(0.0f);
	Interpolator layer(0.0f);
	Interpolator zoom(1.0f);
	InterpolatorAngle angle(0.0f);
	Interpolator scale(1.0f);
	Interpolator posx(0.0f);
	Interpolator posy(0.0f);

	const char* filePath = _argc < 2 ? "" : _argv[1];
	bool directory = false;

	bx::FileInfo fi;
	bx::stat(filePath, fi);
	directory = bx::FileInfo::Directory == fi.m_type;

	std::string path = filePath;
	if (!directory)
	{
		const char* fileName = directory ? filePath : bx::baseName(filePath);
		path.assign(filePath, fileName);
		view.updateFileList(path.c_str(), fileName);
	}
	else
	{
		view.updateFileList(path.c_str() );
	}

	int exitcode = bx::kExitSuccess;
	bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;

	if (view.m_fileList.empty() )
	{
		exitcode = bx::kExitFailure;
		if (2 > _argc)
		{
			help("File path is not specified.");
		}
		else
		{
			fprintf(stderr, "Unable to load '%s' texture.\n", filePath);
		}
	}
	else
	{
		uint32_t fileIndex = 0;
		bool dragging = false;

		entry::MouseState mouseStatePrev;
		entry::MouseState mouseState;
		while (!entry::processEvents(width, height, debug, reset, &mouseState) )
		{
			imguiBeginFrame(mouseState.m_mx
				,  mouseState.m_my
				, (mouseState.m_buttons[entry::MouseButton::Left  ] ? IMGUI_MBUT_LEFT   : 0)
				| (mouseState.m_buttons[entry::MouseButton::Right ] ? IMGUI_MBUT_RIGHT  : 0)
				| (mouseState.m_buttons[entry::MouseButton::Middle] ? IMGUI_MBUT_MIDDLE : 0)
				,  mouseState.m_mz
				, uint16_t(width)
				, uint16_t(height)
				);

			static bool help = false;

			static bool mouseDelta = false;
			if (!mouseDelta)
			{
				mouseStatePrev = mouseState;
				mouseDelta = true;
			}

			int32_t zoomDelta = mouseState.m_mz - mouseStatePrev.m_mz;
			if (zoomDelta != 0)
			{
				char exec[64];
				bx::snprintf(exec, BX_COUNTOF(exec), "view zoom %+f", -zoomDelta*0.1f);
				cmdExec(exec);
			}

			if (mouseState.m_buttons[entry::MouseButton::Left] != mouseStatePrev.m_buttons[entry::MouseButton::Left])
			{
				dragging = !!mouseState.m_buttons[entry::MouseButton::Left];
			}

			if (dragging)
			{
				float xDelta = float(mouseStatePrev.m_mx - mouseState.m_mx);
				float yDelta = float(mouseStatePrev.m_my - mouseState.m_my);

				char exec[64];
				bx::snprintf(exec, BX_COUNTOF(exec), "view pan %+f %+f", xDelta, yDelta);
				cmdExec(exec);
			}

			mouseStatePrev = mouseState;

			if (help == false
			&&  help != view.m_help)
			{
				ImGui::OpenPopup("Help");
			}

			if (ImGui::BeginPopupModal("Help", NULL, ImGuiWindowFlags_AlwaysAutoResize) )
			{
				ImGui::SetWindowFontScale(1.0f);

				ImGui::Text(
					"texturev, bgfx texture viewer tool " ICON_KI_WRENCH ", version %d.%d.%d.\n"
					"Copyright 2011-2017 Branimir Karadzic. All rights reserved.\n"
					"License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause\n"
					, BGFX_TEXTUREV_VERSION_MAJOR
					, BGFX_TEXTUREV_VERSION_MINOR
					, BGFX_API_VERSION
					);
				ImGui::Separator();
				ImGui::NextLine();

				ImGui::Text("Key bindings:\n\n");

				ImGui::PushFont(ImGui::Font::Mono);
				keyBindingHelp("ESC", "Exit.");
				keyBindingHelp("h", "Toggle help screen.");
				keyBindingHelp("f", "Toggle full-screen.");
				ImGui::NextLine();

				keyBindingHelp("LMB+drag",  "Pan.");
				keyBindingHelp("=/- or MW", "Zoom in/out.");
				keyBindingHelp("z/Z",       "Rotate.");
				keyBindingHelp("0",         "Reset.");
				keyBindingHelp("1",         "Fit to window.");
				ImGui::NextLine();

				keyBindingHelp("<",   "Reset MIP level.");
				keyBindingHelp(",/,", "MIP level up/down.");
				keyBindingHelp("/",   "Toggle linear/point texture sampling.");
				ImGui::NextLine();

				keyBindingHelp("left",  "Previous layer in texture array.");
				keyBindingHelp("right", "Next layer in texture array.");
				ImGui::NextLine();

				keyBindingHelp("up",   "Previous texture.");
				keyBindingHelp("down", "Next texture.");
				ImGui::NextLine();

				keyBindingHelp("r/g/b", "Toggle R, G, or B color channel.");
				keyBindingHelp("a",     "Toggle alpha blending.");
				ImGui::NextLine();

				keyBindingHelp("s", "Toggle Multi-channel SDF rendering");
				ImGui::NextLine();

				ImGui::PopFont();

				ImGui::Dummy(ImVec2(0.0f, 0.0f) );
				ImGui::SameLine(ImGui::GetWindowWidth() - 136.0f);
				if (ImGui::Button("Close", ImVec2(128.0f, 0.0f) )
				|| !view.m_help)
				{
					view.m_help = false;
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			help = view.m_help;

			imguiEndFrame();

			if (!bgfx::isValid(texture)
			||  view.m_fileIndex != fileIndex)
			{
				if (bgfx::isValid(texture) )
				{
					bgfx::destroyTexture(texture);
				}

				fileIndex = view.m_fileIndex;

				filePath = view.m_fileList[view.m_fileIndex].c_str();

				texture = loadTexture(filePath
						, 0
						| BGFX_TEXTURE_U_CLAMP
						| BGFX_TEXTURE_V_CLAMP
						| BGFX_TEXTURE_W_CLAMP
						, 0
						, &view.m_info
						);

				std::string title;
				if (isValid(texture) )
				{
					const char* name = "";
					if (view.m_info.cubeMap)
					{
						name = " CubeMap";
					}
					else if (1 < view.m_info.depth)
					{
						name = " 3D";
						view.m_info.numLayers = view.m_info.depth;
					}
					else if (1 < view.m_info.numLayers)
					{
						name = " 2D Array";
					}

					bx::stringPrintf(title, "%s (%d x %d%s, mips: %d, layers %d, %s)"
						, filePath
						, view.m_info.width
						, view.m_info.height
						, name
						, view.m_info.numMips
						, view.m_info.numLayers
						, bimg::getName(bimg::TextureFormat::Enum(view.m_info.format) )
						);
				}
				else
				{
					bx::stringPrintf(title, "Failed to load %s!", filePath);
				}
				entry::WindowHandle handle = { 0 };
				entry::setWindowTitle(handle, title.c_str() );
			}

			int64_t now = bx::getHPCounter();
			static int64_t last = now;
			const int64_t frameTime = now - last;
			last = now;
			const double freq = double(bx::getHPFrequency() );

			time += (float)(frameTime*speed/freq);

			float transitionTime = dragging ? 0.0f : 0.25f;

			posx.set(view.m_posx, transitionTime);
			posy.set(view.m_posy, transitionTime);

			float ortho[16];

			bx::mtxOrtho(ortho, 0.0f, float(width), float(height), 0.0f, 0.0f, 1000.0f, 0.0f, caps->homogeneousDepth);
			bgfx::setViewTransform(BACKGROUND_VIEW_ID, NULL, ortho);
			bgfx::setViewRect(BACKGROUND_VIEW_ID, 0, 0, uint16_t(width), uint16_t(height) );

			screenQuad(
				  0
				, 0
				, width
				, height
				, view.m_alpha ? UINT32_MAX : 0
				, float(width )/float(checkerBoardSize)
				, float(height)/float(checkerBoardSize)
				);
			bgfx::setTexture(0
				, s_texColor
				, checkerBoard
				);
			bgfx::setState(0
				| BGFX_STATE_RGB_WRITE
				| BGFX_STATE_ALPHA_WRITE
				);
			bgfx::submit(BACKGROUND_VIEW_ID
					, textureProgram
					);

			float px = posx.getValue();
			float py = posy.getValue();
			bx::mtxOrtho(ortho, px-width/2, px+width/2, py+height/2, py-height/2, 0.0f, 1000.0f, 0.0f, caps->homogeneousDepth);
			bgfx::setViewTransform(IMAGE_VIEW_ID, NULL, ortho);
			bgfx::setViewRect(IMAGE_VIEW_ID, 0, 0, uint16_t(width), uint16_t(height) );

			bgfx::dbgTextClear();

			if (view.m_fit)
			{
				scale.set(
					bx::fmin( float(width)  / float(view.m_info.width)
					,           float(height) / float(view.m_info.height)
					)
					, 0.1f
					);
			}
			else
			{
				scale.set(1.0f, 0.1f);
			}

			zoom.set(view.m_zoom, transitionTime);
			angle.set(view.m_angle, transitionTime);

			float ss = scale.getValue()
				* zoom.getValue()
				;

			screenQuad(
				  -int(view.m_info.width  * ss)/2
				, -int(view.m_info.height * ss)/2
				,  int(view.m_info.width  * ss)
				,  int(view.m_info.height * ss)
				, view.m_abgr
				);

			float rotz[16];
			bx::mtxRotateZ(rotz, angle.getValue() );
			bgfx::setTransform(rotz);

			float mtx[16];
			bx::mtxRotateXY(mtx, 0.0f, time);
			bgfx::setUniform(u_mtx, mtx);

			mip.set(float(view.m_mip), 0.5f);
			layer.set(float(view.m_layer), 0.25f);

			float params[4] = { mip.getValue(), layer.getValue(), 0.0f, 0.0f };
			if (1 < view.m_info.depth)
			{
				params[1] = layer.getValue()/view.m_info.depth;
			}

			bgfx::setUniform(u_params, params);

			const uint32_t textureFlags = 0
				| BGFX_TEXTURE_U_CLAMP
				| BGFX_TEXTURE_V_CLAMP
				| BGFX_TEXTURE_W_CLAMP
				| (view.m_filter ? 0 : 0
				| BGFX_TEXTURE_MIN_POINT
				| BGFX_TEXTURE_MIP_POINT
				| BGFX_TEXTURE_MAG_POINT
				  )
				;

			bgfx::setTexture(0
				, s_texColor
				, texture
				, textureFlags
				);
			bgfx::setState(0
				| BGFX_STATE_RGB_WRITE
				| BGFX_STATE_ALPHA_WRITE
				| (view.m_alpha ? BGFX_STATE_BLEND_ALPHA : BGFX_STATE_NONE)
				);

			bgfx:: ProgramHandle program = textureProgram;
			if (1 < view.m_info.depth)
			{
				program = texture3DProgram;
			}
			else if (view.m_info.cubeMap)
			{
				program = textureCubeProgram;
			}
			else if (1 < view.m_info.numLayers)
			{
				program = textureArrayProgram;
			}
			else if (view.m_sdf)
			{
				program = textureSdfProgram;
			}

			bgfx::submit(IMAGE_VIEW_ID, program);

			bgfx::frame();
		}
	}

	if (bgfx::isValid(texture) )
	{
		bgfx::destroyTexture(texture);
	}
	bgfx::destroyTexture(checkerBoard);
	bgfx::destroyUniform(s_texColor);
	bgfx::destroyUniform(u_mtx);
	bgfx::destroyUniform(u_params);
	bgfx::destroyProgram(textureProgram);
	bgfx::destroyProgram(textureArrayProgram);
	bgfx::destroyProgram(textureCubeProgram);
	bgfx::destroyProgram(textureSdfProgram);

	imguiDestroy();

	bgfx::shutdown();

	return exitcode;
}

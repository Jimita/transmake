/*  
 *  transmake
 *  Copyright (C) 2020 by Jaime "Lactozilla" Passos

 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

static int parm_argc = 0;
static char **parm_argv = NULL;

static char *parm_palettefile = NULL;
static char *parm_outprefix = NULL;
static char *parm_outfiles = NULL;

//
// STRUCTS AND ENUMS
//

typedef union
{
	unsigned long rgba;
	struct
	{
		unsigned char red;
		unsigned char green;
		unsigned char blue;
		unsigned char alpha;
	} s;
} RGBA_t;

enum alphastyle_e {AST_TRANSLUCENT, AST_ADD, AST_SUBTRACT, AST_REVERSESUBTRACT, AST_MODULATE};
static int blendstyle = AST_TRANSLUCENT;

//
// PROTOTYPES
//

static unsigned char NearestPaletteColor(unsigned char r, unsigned char g, unsigned char b);
static unsigned long ASTBlendPixel(RGBA_t background, RGBA_t foreground, int style, unsigned char alpha);

static void T_OutputMessage(const char *format, ...);
static void T_OutputWarning(const char *format, ...);
static void T_OutputError(const char *format, ...);

//
// MACROS
//

#define min(x,y) (((x)<(y)) ? (x) : (y))
#define max(x,y) (((x)>(y)) ? (x) : (y))

//
// Palette reading
//

#define PALSIZE (256 * 3)
static unsigned char rpalette[PALSIZE];
static RGBA_t palette[PALSIZE];

static void Pal_Read(const char *inpal)
{
	FILE *fp = fopen(inpal, "rb");
	char *pal;
	long size;
	size_t i, j;

	if (fp == NULL)
	{
		T_OutputError("Could not open palette file for reading.");
		exit(EXIT_FAILURE);
	}

	// Get the file size
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);

	if (size < PALSIZE)
	{
		T_OutputError("Palette file has incorrect size!");
		exit(EXIT_FAILURE);
	}

	// Seek back, then read it
	fseek(fp, 0, SEEK_SET);
	if (fread(&rpalette, PALSIZE, 1, fp) != 1)
	{
		T_OutputError("Couldn't read palette file!");
		exit(EXIT_FAILURE);
	}

	// Make RGBA palette
	for (i = 0, j = 0; i < 256, j < PALSIZE; i++, j += 3)
	{
		palette[i].s.red   = rpalette[j];
		palette[i].s.green = rpalette[j + 1];
		palette[i].s.blue  = rpalette[j + 2];
		palette[i].s.alpha = 255;
	}

	T_OutputMessage("Pal_Read: Read palette file successfully!");
}

//
// Blending functions
//

static unsigned char NearestPaletteColor(unsigned char r, unsigned char g, unsigned char b)
{
	int dr, dg, db;
	int distortion, bestdistortion = 256 * 256 * 4, bestcolor = 0, i;

	for (i = 0; i < 256; i++)
	{
		dr = r - palette[i].s.red;
		dg = g - palette[i].s.green;
		db = b - palette[i].s.blue;
		distortion = dr*dr + dg*dg + db*db;
		if (distortion < bestdistortion)
		{
			if (!distortion)
				return (unsigned char)i;

			bestdistortion = distortion;
			bestcolor = i;
		}
	}

	return (unsigned char)bestcolor;
}

static unsigned long ASTBlendPixel(RGBA_t background, RGBA_t foreground, int style, unsigned char alpha)
{
	RGBA_t output;
	signed short fullalpha = (alpha - (0xFF - foreground.s.alpha));
	if (style == AST_TRANSLUCENT)
	{
		if (fullalpha <= 0)
			output.rgba = background.rgba;
		else
		{
			// don't go too high
			if (fullalpha >= 0xFF)
				fullalpha = 0xFF;
			alpha = (unsigned char)fullalpha;

			// if the background pixel is empty,
			// match software and don't blend anything
			if (!background.s.alpha)
				output.s.alpha = 0;
			else
			{
				unsigned char beta = (0xFF - alpha);
				output.s.red = ((background.s.red * beta) + (foreground.s.red * alpha)) / 0xFF;
				output.s.green = ((background.s.green * beta) + (foreground.s.green * alpha)) / 0xFF;
				output.s.blue = ((background.s.blue * beta) + (foreground.s.blue * alpha)) / 0xFF;
				output.s.alpha = 0xFF;
			}
		}
		return output.rgba;
	}
#define clamp(c) max(min(c, 0xFF), 0x00);
	else
	{
		float falpha = ((float)alpha / 256.0f);
		float fr = ((float)foreground.s.red * falpha);
		float fg = ((float)foreground.s.green * falpha);
		float fb = ((float)foreground.s.blue * falpha);
		if (style == AST_ADD)
		{
			output.s.red = clamp((int)(background.s.red + fr));
			output.s.green = clamp((int)(background.s.green + fg));
			output.s.blue = clamp((int)(background.s.blue + fb));
		}
		else if (style == AST_SUBTRACT)
		{
			output.s.red = clamp((int)(background.s.red - fr));
			output.s.green = clamp((int)(background.s.green - fg));
			output.s.blue = clamp((int)(background.s.blue - fb));
		}
		else if (style == AST_REVERSESUBTRACT)
		{
			output.s.red = clamp((int)((-background.s.red) + fr));
			output.s.green = clamp((int)((-background.s.green) + fg));
			output.s.blue = clamp((int)((-background.s.blue) + fb));
		}
		else if (style == AST_MODULATE)
		{
			fr = ((float)foreground.s.red / 256.0f);
			fg = ((float)foreground.s.green / 256.0f);
			fb = ((float)foreground.s.blue / 256.0f);
			output.s.red = clamp((int)(background.s.red * fr));
			output.s.green = clamp((int)(background.s.green * fg));
			output.s.blue = clamp((int)(background.s.blue * fb));
		}

		output.s.alpha = 0xFF;
		return output.rgba;
	}
#undef clamp
	return 0;
}

static unsigned char working[65536];
static void T_BlendTrans(int trans)
{
	float amtmul = (256.0f / 10.0f);
	unsigned char blendamt = (amtmul * trans);
	size_t x, y;

	for (y = 0; y < 256; y++)
	{
		for (x = 0; x < 256; x++)
		{
			unsigned char backcolor = y;
			unsigned char frontcolor = x;
			RGBA_t backrgba = palette[backcolor];
			RGBA_t frontrgba = palette[frontcolor];
			RGBA_t result;

			result.rgba = ASTBlendPixel(backrgba, frontrgba, blendstyle, blendamt);
			working[((y * 256) + x)] = NearestPaletteColor(result.s.red, result.s.green, result.s.blue);
		}
	}
}

static char *defaultprefix = "TRANS";
static char *defaultoutfiles = "123456789";

static void T_DoMain(void)
{
	FILE *fp;
	char *filename;
	char *prefix = (parm_outprefix == NULL) ? defaultprefix : parm_outprefix;
	char *outfiles = (parm_outfiles == NULL) ? defaultoutfiles : parm_outfiles;
	int canblend[9];
	int i;

	// allocate memory for the filename string
	filename = malloc(strlen(prefix) + 6);
	if (!filename)
	{
		T_OutputError("Not enough memory for filename");
		exit(EXIT_FAILURE);
	}

	// figure out which lump to blend or not
	for (i = 0; i < 9; i++)
		canblend[i] = 0;
	for (i = 0; i < strlen(outfiles); i++)
	{
		if (isdigit(outfiles[i]))
			canblend[((outfiles[i]) - '0') - 1] = 1;
	}

	// blend every lump
	for (i = 1; i <= 9; i++)
	{
		// don't blend this one
		if (!canblend[i-1])
			continue;

		// make the filename string
		sprintf(filename, "%s%d0.lmp", prefix, i);
		T_OutputMessage("Writing %s...", filename);

		// open, blend, write, close.
		fp = fopen(filename, "wb");
		T_BlendTrans(i);
		fwrite(&working, 1, 65536, fp);
		fclose(fp);
	}

	// free the filename string
	free(filename);
	T_OutputMessage("Done!");
}

//
// Informative functions
//

static void T_PrintCopyrightText(FILE *outstream, int full)
{
	fprintf(outstream,
	"transmake\n"
	"Copyright (C) 2020 by Jaime \"Lactozilla\" Passos\n\n");

	if (full)
		fprintf(outstream,
		"This program is free software: you can redistribute it and/or modify\n"
		"it under the terms of the GNU General Public License as published by\n"
		"the Free Software Foundation, either version 3 of the License, or\n"
		"(at your option) any later version.\n\n"

		"This program is distributed in the hope that it will be useful,\n"
		"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
		"GNU General Public License for more details.\n\n"

		"You should have received a copy of the GNU General Public License\n"
		"along with this program. If not, see <https://www.gnu.org/licenses/>.\n\n"
		);
}

static void T_PrintParmInfo(FILE *outstream)
{
	fprintf(outstream,
	"Command line parameters:\n"

	"    -palette    <palname>   :\n"
	"                            Specify the input palette file. Required.\n"
	"    -outfiles   <123456789> :\n"
	"                            Specify the output files. Not required.\n"
	"                            For example, specifying \"135\" will\n"
	"                            output \"TRANS10\", \"TRANS30\" and \"TRANS50\".\n"
	"    -outprefix  <prefix>    :\n"
	"                            Specify the output prefix. Not required.\n"
	"                            Defaults to \"TRANS\".\n"
	"    -blendstyle <style>     :\n"
	"                            Specify the blend style. Not required.\n"
	"                            Defaults to \"translucent\".\n"
	"                            Available blend modes:\n"
	"                              -  translucent, add, subtract,\n"
	"                              -  reversesubtract, modulate\n"

	);
}

#define VA_OUTPUT \
	char out[1024] = ""; \
	va_list list; \
 \
	va_start(list, format); \
	vsnprintf(out, 1024, format, list); \
	va_end(list); \

static void T_OutputMessage(const char *format, ...)
{
	VA_OUTPUT;
	fprintf(stdout, out);
	fprintf(stdout, "\n");
}

static void T_OutputWarning(const char *format, ...)
{
	VA_OUTPUT;
	fprintf(stdout, "WARNING: ");
	fprintf(stdout, out);
	fprintf(stdout, "\n");
}

static void T_OutputError(const char *format, ...)
{
	VA_OUTPUT;
	fprintf(stdout, "ERROR: ");
	fprintf(stdout, out);
	fprintf(stdout, "\n");
}

#undef VA_OUTPUT

//
// Parse command line arguments
//

static int Parm_ParseParameter(char *parmstring, int p_argc)
{
	if (p_argc >= parm_argc)
		return p_argc+1;

	if (!stricmp(parmstring, "palette"))
		parm_palettefile = parm_argv[p_argc];
	else if (!stricmp(parmstring, "outfiles"))
	{
		char *outfiles = parm_argv[p_argc];
		if (outfiles[0])
			parm_outfiles = outfiles;
	}
	else if (!stricmp(parmstring, "outprefix"))
		parm_outprefix = parm_argv[p_argc];
	else if (!stricmp(parmstring, "blendstyle"))
	{
		char *stylestring = parm_argv[p_argc];
		if (!stricmp(stylestring, "translucent"))
			blendstyle = AST_TRANSLUCENT;
		else if (!stricmp(stylestring, "add"))
			blendstyle = AST_ADD;
		else if (!stricmp(stylestring, "subtract"))
			blendstyle = AST_SUBTRACT;
		else if (!stricmp(stylestring, "reversesubtract"))
			blendstyle = AST_REVERSESUBTRACT;
		else if (!stricmp(stylestring, "modulate"))
			blendstyle = AST_MODULATE;
	}

	return p_argc+1;
}

static void Parm_Parse(void)
{
	int i;
	for (i = 1; i < parm_argc;)
	{
		char *parmstring = parm_argv[i];
		if (parmstring[0] == '-' && parmstring[1])
			i = Parm_ParseParameter(parmstring+1, i+1);
		else
			i++;
	}
}

//
// Main function
//

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		T_PrintCopyrightText(stderr, 1);
		T_PrintParmInfo(stderr);
		exit(EXIT_FAILURE);
	}
	else
		T_PrintCopyrightText(stdout, 0);

	parm_argv = argv;
	parm_argc = argc;
	Parm_Parse();

	if (parm_palettefile == NULL)
	{
		T_OutputError("Palette file not specified. Use the -palette parameter.");
		exit(EXIT_FAILURE);
	}
	else
		Pal_Read(parm_palettefile);

	T_DoMain();
	return 0;
}

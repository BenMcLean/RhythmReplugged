#include "BitmapFont.h"

#include <SDL_image.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace
{
	std::filesystem::path GetFontAtlasPath()
	{
		const std::filesystem::path sourceDir = std::filesystem::path(__FILE__).parent_path();
		return sourceDir.parent_path() / "assets" / "Bm437_IBM_VGA_9x16.png";
	}
}

namespace rhythmreplugged
{
	BitmapFont::~BitmapFont()
	{
		if (texture_)
			SDL_DestroyTexture(texture_);
	}

	bool BitmapFont::Load(SDL_Renderer *renderer)
	{
		if (texture_)
			return true;

		const std::filesystem::path fontAtlasPath = GetFontAtlasPath();
		texture_ = IMG_LoadTexture(renderer, fontAtlasPath.string().c_str());
		if (!texture_)
		{
			std::cerr << "IMG_LoadTexture failed for " << fontAtlasPath.string()
					  << ": " << IMG_GetError() << "\n";
			return false;
		}

		SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
		SDL_SetTextureColorMod(texture_, 255, 245, 230);
		return true;
	}

	void BitmapFont::DrawText(SDL_Renderer *renderer, const std::string &text, int startX, int startY, int scale) const
	{
		if (!texture_)
			return;

		int x = startX;
		int y = startY;

		for (unsigned char c : text)
		{
			if (c == '\n')
			{
				x = startX;
				y += GlyphHeight * scale;
				continue;
			}

			const SDL_Rect sourceRect{
				(c % GlyphsPerRow) * GlyphWidth,
				(c / GlyphsPerRow) * GlyphHeight,
				GlyphWidth,
				GlyphHeight};

			const SDL_Rect destRect{
				x,
				y,
				GlyphWidth * scale,
				GlyphHeight * scale};

			SDL_RenderCopy(renderer, texture_, &sourceRect, &destRect);
			x += GlyphWidth * scale;
		}
	}

	int BitmapFont::MeasureTextWidth(const std::string &text, int scale) const
	{
		int currentLineWidth = 0;
		int maxLineWidth = 0;

		for (unsigned char c : text)
		{
			if (c == '\n')
			{
				maxLineWidth = std::max(maxLineWidth, currentLineWidth);
				currentLineWidth = 0;
				continue;
			}

			currentLineWidth += GlyphWidth * scale;
		}

		return std::max(maxLineWidth, currentLineWidth);
	}

	int BitmapFont::MeasureTextHeight(const std::string &text, int scale) const
	{
		int lineCount = 1;
		for (char c : text)
		{
			if (c == '\n')
				++lineCount;
		}

		return lineCount * GlyphHeight * scale;
	}
}

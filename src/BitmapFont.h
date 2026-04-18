#pragma once

#include <SDL.h>

#include <string>

namespace rhythmreplugged
{
	class BitmapFont
	{
	public:
		static constexpr int GlyphWidth = 9;
		static constexpr int GlyphHeight = 16;
		static constexpr int GlyphsPerRow = 16;

		BitmapFont() = default;
		~BitmapFont();

		BitmapFont(const BitmapFont &) = delete;
		BitmapFont &operator=(const BitmapFont &) = delete;

		bool Load(SDL_Renderer *renderer);
		void DrawText(SDL_Renderer *renderer, const std::string &text, int startX, int startY, int scale = 1) const;
		int MeasureTextWidth(const std::string &text, int scale = 1) const;
		int MeasureTextHeight(const std::string &text, int scale = 1) const;

	private:
		SDL_Texture *texture_ = nullptr;
	};
}

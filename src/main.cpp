#include "BitmapFont.h"

#include <SDL.h>
#include <SDL_image.h>

#include <iostream>
#include <string>

namespace
{
	constexpr int kWindowWidth = 800;
	constexpr int kWindowHeight = 600;
}

int main(int argc, char *argv[])
{
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
		return 1;
	}

	const int imageInitFlags = IMG_INIT_PNG;
	if ((IMG_Init(imageInitFlags) & imageInitFlags) != imageInitFlags)
	{
		std::cerr << "IMG_Init failed: " << IMG_GetError() << "\n";
		SDL_Quit();
		return 1;
	}

	SDL_Window *window = SDL_CreateWindow(
		"Rhythm Replugged - Hello World",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		kWindowWidth,
		kWindowHeight,
		SDL_WINDOW_SHOWN);

	if (!window)
	{
		std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	SDL_Renderer *renderer = SDL_CreateRenderer(
		window,
		-1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	if (!renderer)
	{
		std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
		SDL_DestroyWindow(window);
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	rhythmreplugged::BitmapFont font;
	if (!font.Load(renderer))
	{
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	const std::string message = "Hello World!";
	const int scale = 3;
	const int textX = (kWindowWidth - font.MeasureTextWidth(message, scale)) / 2;
	const int textY = (kWindowHeight - font.MeasureTextHeight(message, scale)) / 2;

	bool running = true;
	SDL_Event e;

	while (running)
	{
		while (SDL_PollEvent(&e))
		{
			if (e.type == SDL_QUIT)
				running = false;
		}

		SDL_SetRenderDrawColor(renderer, 18, 28, 46, 255);
		SDL_RenderClear(renderer);
		font.DrawText(renderer, message, textX, textY, scale);
		SDL_RenderPresent(renderer);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	IMG_Quit();
	SDL_Quit();

	return 0;
}

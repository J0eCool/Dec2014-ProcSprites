#include <SDL.h>
#include <SDL_image.h>

#include <stdio.h>

int main(int argc, char** argv) {
	SDL_Init(SDL_INIT_VIDEO);
	IMG_Init(IMG_INIT_PNG);
	int screenWidth = 800;
	int screenHeight = 600;
	auto window = SDL_CreateWindow("Procedural Sprites", 
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screenWidth, screenHeight, SDL_WINDOW_SHOWN);
	auto renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

	int texWidth = 160;
	int texHeight = 40;
	SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
		SDL_TEXTUREACCESS_STATIC, texWidth, texHeight);

	Uint32 *pixels = new Uint32[texWidth * texHeight];
	auto makeRGB = [](Uint8 r, Uint8 g, Uint8 b) {
		Uint8 a = 0xff;
		return (a << 24) |
			(b << 16) |
			(g << 8) |
			r;
	};
	auto makeGray = [makeRGB](Uint8 c) {
		return makeRGB(c, c, c);
	};
	auto clamp = [](float t, float lo, float hi) {
		if (t < lo) { return lo; }
		if (t > hi) { return hi; }
		return t;
	};
	auto clamp01 = [clamp](float t) { return clamp(t, 0.0f, 1.0f); };
	auto lerp = [clamp01](float t, int lo, int hi) {
		return (int)(lo + clamp01(t) * (hi - lo));
	};

	int w2 = texWidth / 2;
	int h2 = texHeight / 2;
	for (int y = 0; y < texHeight; ++y) {
		for (int x = 0; x < texWidth; ++x) {
			float t = (float)SDL_sqrt((x - w2) * (x - w2) / float(w2 * w2) +
				(y - h2) * (y - h2) / float(h2 * h2));
			pixels[x + y * texWidth] = makeGray(lerp(t, 0xff, 0x00));
		}
	}

	SDL_UpdateTexture(texture, nullptr, pixels, texWidth * sizeof(Uint32));

	bool quit = false;
	SDL_Event event;
	while (!quit) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				quit = true;
			}
			else if (event.type == SDL_KEYDOWN) {
				if (event.key.keysym.sym == SDLK_ESCAPE) {
					quit = true;
				}
			}
		}

		SDL_RenderClear(renderer);

		float scale = 4.0f;
		SDL_Rect rect;
		rect.w = (int)(scale * texWidth);
		rect.h = (int)(scale * texHeight);
		rect.x = (screenWidth - rect.w) / 2;
		rect.y = (screenHeight - rect.h) / 2;

		SDL_RenderCopy(renderer, texture, nullptr, &rect);

		SDL_RenderPresent(renderer);
	}

	delete[] pixels;
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);

	IMG_Quit();
	SDL_Quit();

	return 0;
}

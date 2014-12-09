#if defined(MAC)
	#include <SDL2/SDL.h>
	#include <SDL2_image/SDL_image.h>
#else
	#include <SDL.h>
	#include <SDL_image.h>
#endif

#undef _DEBUG
#include <iostream>
#include <unordered_set>
#include <stdio.h>
#include <vector>

using namespace std;

struct Point {
	int x, y;
	bool operator==(const Point &p) const { return x == p.x && y == p.y; }
};

namespace std { template <> struct hash < Point > { size_t operator()(const Point &p) const { return hash<int>()(p.x ^ p.y); } }; }

inline Uint32 getPixel(SDL_Surface *surface, int x, int y) {
	int bpp = surface->format->BytesPerPixel;
	Uint8 *p = (Uint8 *)surface->pixels + x * bpp + y * surface->pitch;
	switch (bpp) {
	case 1:
		return *p;
	case 2:
		return *(Uint16 *)p;
	case 4:
		return *(Uint32 *)p;
	default:
		return 0;
	}
}

inline void setPixel(SDL_Surface *surface, int x, int y, Uint32 pixel) {
	int bpp = surface->format->BytesPerPixel;
	Uint8 *p = (Uint8 *)surface->pixels + x * bpp + y * surface->pitch;
	switch (bpp) {
	case 1:
		*p = (Uint8)pixel;
		break;
	case 2:
		*(Uint16 *)p = (Uint16)pixel;
		break;
	case 4:
		*(Uint32 *)p = pixel;
		break;
	}
}

inline Uint8 getAlpha(SDL_Surface *surface, int x, int y) {
	auto pixel = getPixel(surface, x, y);
	Uint8 r, g, b, a;
	SDL_GetRGBA(pixel, surface->format, &r, &g, &b, &a);
	return a;
}

inline SDL_Rect floodFill(SDL_Surface *surface, int x, int y) {
	unordered_set<Point> open;
	decltype(open) closed;

	open.insert({ x, y });
	closed.insert({ x, y });

	vector<Point> dirs{ { -1, 0 }, { 0, 1 }, { 1, 0 }, { 0, -1 } };
	while (!open.empty()) {
		auto p = *open.begin();
		open.erase(open.begin());

		for (auto d : dirs) {
			int i = p.x + d.x;
			int j = p.y + d.y;
			Point p_{ i, j };
			if (i >= 0 && j >= 0 && i < surface->w && j <= surface->h &&
					closed.find(p_) == closed.end() &&
					getAlpha(surface, i, j)) {
				open.insert(p_);
				closed.insert(p_);
			}
		}
	}

	int left = INT_MAX;
	int right = 0;
	int top = INT_MAX;
	int bot = 0;
	for (auto p : closed) {
		left = SDL_min(p.x, left);
		right = SDL_max(p.x, right);
		top = SDL_min(p.y, top);
		bot = SDL_max(p.y, bot);
	}

	SDL_Rect rect;
	rect.x = left;
	rect.y = top;
	rect.w = right - left + 1;
	rect.h = bot - top + 1;
	return rect;
};

vector<SDL_Rect> getSprites(SDL_Surface *surface) {
	vector<SDL_Rect> rects;
	auto alreadyCounted = [&rects](int x, int y) {
		for (auto r : rects) {
			if (x >= r.x && x < r.x + r.w &&
					y >= r.y && y < r.y + r.h) {
				return true;
			}
		}
		return false;
	};
	for (int y = 0; y < surface->h; ++y) {
		for (int x = 0; x < surface->w; ++x) {
			if (getAlpha(surface, x, y) && !alreadyCounted(x, y)) {
				rects.push_back(floodFill(surface, x, y));
			}
		}
	}
	return rects;
}

int main(int argc, char** argv) {
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		cout << "SDL could not initialize: Error: " << SDL_GetError() << endl;
	}
	int imgFlags = IMG_INIT_PNG;
	if (!(IMG_Init(imgFlags) & imgFlags)) {
		cout << "SDL_image could not initialize: Error: " << IMG_GetError() << endl;
	}
	int screenWidth = 800;
	int screenHeight = 600;
	auto window = SDL_CreateWindow("Procedural Sprites", 
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screenWidth, screenHeight, SDL_WINDOW_SHOWN);
	if (!window) {
		cout << "Window could not be created: Error: " << SDL_GetError() << endl;
	}
	auto renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (!renderer) {
		cout << "Renderer could not be created: Error: " << SDL_GetError() << endl;
	}

	auto inputSurface = IMG_Load("../Input/MarioSpritesheet.png");
	auto inputRects = getSprites(inputSurface);

	auto white = SDL_MapRGBA(inputSurface->format, 0xff, 0xff, 0xff, 0xff);
	auto black = SDL_MapRGBA(inputSurface->format, 0x00, 0x00, 0x00, 0xff);
	for (auto r : inputRects) {
		for (int y = r.y; y < r.y + r.h; ++y) {
			for (int x = r.x; x < r.x + r.w; ++x) {
				auto pixel = getAlpha(inputSurface, x, y) ? white : black;
				setPixel(inputSurface, x, y, pixel);
			}
		}
	}

	auto inputTexture = SDL_CreateTextureFromSurface(renderer, inputSurface);

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
	int inIndex = 0;
	while (!quit) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				quit = true;
			}
			else if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					quit = true;
					break;
				case SDLK_LEFT:
					inIndex = (inIndex + inputRects.size() - 1) % inputRects.size();
					break;
				case SDLK_RIGHT:
					inIndex = (inIndex + 1) % inputRects.size();
					break;
				}
			}
		}

		SDL_RenderClear(renderer);

		float scale = 16.0f;
		// SDL_Rect rect;
		// rect.w = (int)(scale * texWidth);
		// rect.h = (int)(scale * texHeight);
		// rect.x = (screenWidth - rect.w) / 2;
		// rect.y = (screenHeight - rect.h) / 2;
		// SDL_RenderCopy(renderer, texture, nullptr, &rect);

		if (inputRects.size() > 0) {
			SDL_Rect inR = inputRects[inIndex];
			SDL_Rect inRDest = inR;
			inRDest.x = 30;
			inRDest.y = 30;
			inRDest.w = (int)(inRDest.w * scale);
			inRDest.h = (int)(inRDest.h * scale);
			SDL_RenderCopy(renderer, inputTexture, &inR, &inRDest);
		}

		SDL_RenderPresent(renderer);
	}

	delete[] pixels;
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_FreeSurface(inputSurface);

	IMG_Quit();
	SDL_Quit();

	return 0;
}

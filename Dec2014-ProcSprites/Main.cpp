#if defined(MAC)
	#include <SDL2/SDL.h>
	#include <SDL2_image/SDL_image.h>
#else
	#include <SDL.h>
	#include <SDL_image.h>
#endif

#undef _DEBUG
#include <iostream>
#include <stdio.h>
#include <unordered_map>
#include <unordered_set>
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

class SpriteMarkov {
private:
	typedef Uint32 Color;
	typedef Uint64 Input;
	typedef unordered_map<Color, int> Counts;
	typedef unordered_map<Color, float> Probability;
	unordered_map<Input, Counts> _data;
	unordered_map<Input, Probability> _probabilities;

	Color _blackColor;
	Color _whiteColor;
	Input _blackInput;
	Input _whiteInput;
	Input _eof;
	int _bpp;

	Color *_generatedPixels = nullptr;

public:
	template <typename T>
	Input getPrev(T f, SDL_Rect const& r, int x, int y) {
		Input prev = 0;
		static const int kCount = 3;
		Point ps[] = { {-1, 0}, {0, -1},
			{-2, 0}, {-1, -1}, {0, -2},
			{-3, 0}, {-2, -1}, {-1, -2}, {0, -3} };
		for (Point& p : ps) {
			p = { x + p.x, y + p.y };
		}
		for (int i = 0; i < kCount; ++i) {
			Input in;
			if (ps[i].x < r.x || ps[i].y < r.y) {
				in = _eof;
			}
			else {
				in = f(ps[i]);
			}
			prev |= in << (2 * i);
		}
		return prev;
	}

	SpriteMarkov(SDL_Surface *input) {
		auto rects = getSprites(input);

		_bpp = input->format->BytesPerPixel;

		_whiteColor = SDL_MapRGBA(input->format, 0xff, 0xff, 0xff, 0xff);
		_blackColor = SDL_MapRGBA(input->format, 0x00, 0x00, 0x00, 0xff);

		_blackInput = 0x0;
		_whiteInput = 0x1;
		_eof = 0x2;

		auto getColor = [this, input](Point p) {
			return getAlpha(input, p.x, p.y) ? _whiteInput : _blackInput;
		};
		for (auto r : rects) {
			for (int y = r.y; y < r.y + r.h; ++y) {
				for (int x = r.x; x < r.x + r.w; ++x) {
					Input prev = getPrev(getColor, r, x, y);

					Color cur = getAlpha(input, x, y) ? _whiteColor : _blackColor;
					_data[prev][cur] += 1;
					prev = cur;
				}
			}
		}

		for (auto inCount : _data) {
			int count = 0;
			for (auto colCount : inCount.second) {
				count += colCount.second;
			}
			Probability probs;
			for (auto colCount : inCount.second) {
				probs[colCount.first] = (float)colCount.second / count;
			}
			_probabilities[inCount.first] = probs;
		}
	}

	Color getNext(Input prev) {
		float r = (float)rand() / RAND_MAX;
		for (auto colCount : _probabilities[prev]) {
			r -= colCount.second;
			if (r <= 0.0f) {
				return colCount.first;
			}
		}
		return 0;
	}

	void* CreatePixelData(int width, int height) {
		if (_generatedPixels) {
			delete[] _generatedPixels;
		}
		_generatedPixels = new Color[width * height * _bpp];

		auto getColor = [this, width](Point p) {
			return _generatedPixels[p.x + p.y * width] == _whiteColor ? _whiteInput : _blackInput;
		};
		SDL_Rect rect;
		rect.x = 0;
		rect.y = 0;
		rect.w = width;
		rect.h = height;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				Input prev = getPrev(getColor, rect, x, y);
				Color cur = getNext(prev);
				_generatedPixels[1 * (x + y * width)] = cur;
				prev = cur;
			}
		}

		return _generatedPixels;
	}
};

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

	// auto makeRGB = [](Uint8 r, Uint8 g, Uint8 b) {
	// 	Uint8 a = 0xff;
	// 	return (a << 24) |
	// 		(b << 16) |
	// 		(g << 8) |
	// 		r;
	// };
	// auto makeGray = [makeRGB](Uint8 c) {
	// 	return makeRGB(c, c, c);
	// };
	// auto clamp = [](float t, float lo, float hi) {
	// 	if (t < lo) { return lo; }
	// 	if (t > hi) { return hi; }
	// 	return t;
	// };
	// auto clamp01 = [clamp](float t) { return clamp(t, 0.0f, 1.0f); };
	// auto lerp = [clamp01](float t, int lo, int hi) {
	// 	return (int)(lo + clamp01(t) * (hi - lo));
	// };

	auto inputSurface = IMG_Load("../Input/MarioSpritesheet.png");

	int texWidth = 30;
	int texHeight = 30;
	SDL_Texture *texture = SDL_CreateTexture(renderer, inputSurface->format->format,
		SDL_TEXTUREACCESS_STATIC, texWidth, texHeight);

	SpriteMarkov markov(inputSurface);
	auto pixels = markov.CreatePixelData(texWidth, texHeight);

	SDL_UpdateTexture(texture, nullptr, pixels, texWidth * inputSurface->format->BytesPerPixel);

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
				case SDLK_SPACE:
					pixels = markov.CreatePixelData(texWidth, texHeight);
					SDL_UpdateTexture(texture, nullptr, pixels, texWidth * inputSurface->format->BytesPerPixel);
					// inIndex = (inIndex + inputRects.size() - 1) % inputRects.size();
					break;
				case SDLK_RIGHT:
					// inIndex = (inIndex + 1) % inputRects.size();
					break;
				}
			}
		}

		SDL_RenderClear(renderer);

		float scale = 16.0f;
		SDL_Rect rect;
		rect.w = (int)(scale * texWidth);
		rect.h = (int)(scale * texHeight);
		rect.x = (screenWidth - rect.w) / 2;
		rect.y = (screenHeight - rect.h) / 2;
		SDL_RenderCopy(renderer, texture, nullptr, &rect);

		SDL_RenderPresent(renderer);
	}

	// delete[] pixels;
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_FreeSurface(inputSurface);

	IMG_Quit();
	SDL_Quit();

	return 0;
}

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
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

struct Point {
	int x, y;
	bool operator==(const Point &p) const { return x == p.x && y == p.y; }
};

namespace std { template <> struct hash < Point > { size_t operator()(const Point &p) const { return hash<int>()(p.x ^ p.y); } }; }

float clamp(float t, float lo, float hi) {
	if (t < lo) { return lo; }
	if (t > hi) { return hi; }
	return t;
};
float clamp01(float t) { return clamp(t, 0.0f, 1.0f); };
float lerp(float t, int lo, int hi) {
	return (int)(lo + clamp01(t) * (hi - lo));
};

inline Uint32 getRawPixel(void* pixels, int bpp, int pitch, int x, int y) {
	Uint8 *p = (Uint8 *)pixels + x * bpp + y * pitch;
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

inline Uint32 getPixel(SDL_Surface *surface, int x, int y) {
	int bpp = surface->format->BytesPerPixel;
	int pitch = surface->pitch;
	return getRawPixel(surface->pixels, bpp, pitch, x, y);
}

inline void setRawPixel(void* pixels, int bpp, int pitch, int x, int y, Uint32 color) {
	Uint8 *p = (Uint8 *)pixels + x * bpp + y * pitch;
	switch (bpp) {
	case 1:
		*p = (Uint8)color;
		break;
	case 2:
		*(Uint16 *)p = (Uint16)color;
		break;
	case 4:
		*(Uint32 *)p = color;
		break;
	}	
}

inline void setPixel(SDL_Surface *surface, int x, int y, Uint32 color) {
	int bpp = surface->format->BytesPerPixel;
	int pitch = surface->pitch;
	setRawPixel(surface->pixels, bpp, pitch, x, y, color);
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
	typedef string Input;
	typedef char InputAtom;
	typedef unordered_map<Color, int> Counts;
	typedef unordered_map<Color, float> Probability;
	unordered_map<Input, Counts> _data;
	unordered_map<Input, Probability> _probabilities;

	SDL_Surface *_inputSurface;

	Color _blackColor;
	Color _whiteColor;
	InputAtom _blackInput;
	InputAtom _whiteInput;
	InputAtom _eof;
	int _bpp;

	Uint8 *_generatedPixels = nullptr;
	Uint8 *_probabilityPixels = nullptr;
	bool _showProbability = false;

public:
	template <typename T>
	Input getPrev(T f, SDL_Rect const& r, int x, int y) {
		static const int kMaxDx = 2;
		static bool didInitialize = false;
		static vector<Point> ds;
		if (!didInitialize) {
			didInitialize = true;
			for (int i = 0; i <= kMaxDx; ++i) {
				for (int j = 0; j <= kMaxDx - i; ++j) {
					if (i == 0 && j == 0) { continue; }
					ds.push_back({ -i, -j });
				}
			}
		}

		vector<Point> ps;
		for (Point& d : ds) {
			ps.push_back({ x + d.x, y + d.y });
		}

		Input prev = "";
		for (Point p : ps) {
			InputAtom in;
			if (p.x < r.x || p.y < r.y) {
				in = _eof;
			}
			else {
				in = f(p);
			}
			prev += in;
		}
		return prev;
	}

	Color lerpColor(float t) {
		Uint8 c = lerp(t, 0x00, 0xff);
		return SDL_MapRGBA(_inputSurface->format, c, c, c, 0xff);
	}

	SpriteMarkov(SDL_Surface *input) {
		auto rects = getSprites(input);

		_bpp = input->format->BytesPerPixel;

		_inputSurface = input;

		_whiteColor = SDL_MapRGBA(input->format, 0xff, 0xff, 0xff, 0xff);
		_blackColor = SDL_MapRGBA(input->format, 0x00, 0x00, 0x00, 0xff);

		_blackInput = 'b';
		_whiteInput = 'w';
		_eof = '$';

		auto getColor = [this, input](Point p) {
			return getAlpha(input, p.x, p.y) ? _whiteInput : _blackInput;
		};
		for (auto r : rects) {
			for (int y = r.y; y < r.y + r.h; ++y) {
				for (int x = r.x; x < r.x + r.w; ++x) {
					Input prev = getPrev(getColor, r, x, y);

					Color cur = getAlpha(input, x, y) ? _whiteColor : _blackColor;
					_data[prev][cur] += 1;
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
			delete[] _probabilityPixels;
		}
		_generatedPixels = new Uint8[width * height * _bpp];
		_probabilityPixels = new Uint8[width * height * _bpp];

		auto getColor = [this, width](Point p) {
			return getRawPixel(_generatedPixels, _bpp, _bpp * width, p.x, p.y) == _whiteColor ? _whiteInput : _blackInput;
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
				setRawPixel(_generatedPixels, _bpp, _bpp * width, x, y, cur);

				float probability = _probabilities[prev][cur];
				setRawPixel(_probabilityPixels, _bpp, _bpp * width, x, y, lerpColor(probability));
			}
		}

		return _showProbability ? _probabilityPixels : _generatedPixels;
	}

	void* ToggleProbabilityPixels() {
		_showProbability = !_showProbability;
		return _showProbability ? _probabilityPixels : _generatedPixels;
	}

	void PrintProbabilities() {
		for (auto inProb : _probabilities) {
			cout << "In: " << inProb.first << '\n';
			for (auto colProb : inProb.second) {
				char c = colProb.first == _blackColor ? 'b' : 'w';
				cout << '\t' << c << " : " << colProb.second << '\n';
			}
		}
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

	auto inputSurface = IMG_Load("../Input/MarioSpritesheet.png");

	int texSize = 32;

	SpriteMarkov markov(inputSurface);

	SDL_Texture *texture = nullptr;
	auto rebuildTexture = [&texture, &texSize, inputSurface, renderer]() {
		if (texture) SDL_DestroyTexture(texture);
		texture = SDL_CreateTexture(renderer, inputSurface->format->format,
			SDL_TEXTUREACCESS_STATIC, texSize, texSize);
	};
	auto buildSprite = [&texture, &texSize, inputSurface](void* pixels) {
		SDL_UpdateTexture(texture, nullptr, pixels, texSize * inputSurface->format->BytesPerPixel);
	};
	auto remakeSprite = [&texture, &texSize, inputSurface, &markov, buildSprite]() {
		auto pixels = markov.CreatePixelData(texSize, texSize);
		buildSprite(pixels);
	};

	rebuildTexture();
	remakeSprite();

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
					buildSprite(markov.ToggleProbabilityPixels());
					break;

				case SDLK_RIGHT:
				case SDLK_LEFT:
					remakeSprite();
					break;

				case SDLK_UP:
					texSize++;
					rebuildTexture();
					remakeSprite();
					break;
				case SDLK_DOWN:
					texSize = SDL_max(texSize - 1, 0);
					rebuildTexture();
					remakeSprite();
					break;
				}
			}
		}

		SDL_RenderClear(renderer);

		float scale = 16.0f;
		SDL_Rect rect;
		rect.w = (int)(scale * texSize);
		rect.h = (int)(scale * texSize);
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

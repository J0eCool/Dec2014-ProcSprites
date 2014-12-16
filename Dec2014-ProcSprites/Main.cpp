#if defined(MAC)
	#include <SDL2/SDL.h>
	#include <SDL2_image/SDL_image.h>
#else
	#include <SDL.h>
	#include <SDL_image.h>
#endif

#undef _DEBUG
#include <boost/filesystem.hpp>
#include <iostream>
#include <map>
#include <stdio.h>
#include <string>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

struct MarkovArgs {
	int lookahead = 1;
	int imageDivisions = 4;
	bool useColor = false;
	int biasTerm = 5;
	bool printCounts = false;
	int passes = 1;
	string inputFolder = "Input/";
	string outputFolder = "";
	int numOutputImages = 100;
};
MarkovArgs gArgs;

void parseInput(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (argv[i] == string("--color")) {
			gArgs.useColor = true;
		}
		else if (argv[i] == string("--print-count")) {
			gArgs.printCounts = true;
		} 
		else if (argv[i] == string("--lookahead")) {
			gArgs.lookahead = atoi(argv[i + 1]);
			i += 1;
		}
		else if (argv[i] == string("--divisions")) {
			gArgs.imageDivisions = atoi(argv[i + 1]);
			i += 1;
		}
		else if (argv[i] == string("--bias")) {
			gArgs.biasTerm = atoi(argv[i + 1]);
			i += 1;
		}
		else if (argv[i] == string("--passes")) {
			gArgs.passes = atoi(argv[i + 1]);
			i += 1;
		}
		else if (argv[i] == string("--input")) {
			gArgs.inputFolder = argv[i + 1];
			i += 1;
		}
		else if (argv[i] == string("--output")) {
			gArgs.outputFolder = argv[i + 1];
			i += 1;
		}
		else if (argv[i] == string("--output-count")) {
			gArgs.numOutputImages = atoi(argv[i + 1]);
			i += 1;
		}
	}
}

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

	SDL_PixelFormat *_inputFormat;

	int _bpp;
	int _width;
	int _height;

	Uint8 *_generatedPixels = nullptr;

	bool _initialized = false;

public:
	template <typename T>
	Input getPrev(T f, SDL_Rect const& r, int x, int y) {
		static bool didInitialize = false;
		static vector<Point> ds;
		if (!didInitialize) {
			didInitialize = true;
			for (int i = 0; i <= gArgs.lookahead; ++i) {
				for (int j = 0; j <= gArgs.lookahead - i; ++j) {
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
		static auto divNum = [](int pos, int start, int size) {
			for (int i = 0; i < gArgs.imageDivisions; ++i) {
				if (pos < start + i * size / gArgs.imageDivisions) {
					return '0' + i;
				}
			}
			return '0' + gArgs.imageDivisions;
		};
		prev += divNum(x, r.x, r.w);
		prev += divNum(y, r.y, r.h);

		for (Point p : ps) {
			Input in;
			if (p.x < r.x || p.y < r.y) {
				in = '$';
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
		return SDL_MapRGBA(_inputFormat, c, c, c, 0xff);
	}

	vector<Uint8> _thresholds { 0x40, 0x80, 0xb0, 0xff };
	InputAtom atomForValue(Uint8 c) {
		for (int i = 0; i < _thresholds.size(); ++i) {
			if (c <= _thresholds[i]) {
				return '0' + i;
			}
		}
		return '0' + _thresholds.size();
	}
	Uint8 valueForAtom(InputAtom i) {
		int idx = i - '0';
		if (i == '_' || idx < 0 || idx >= _thresholds.size()) {
			return 0;
		}
		return _thresholds[idx];
	}

	Input inputForColor(Color c) {
		Uint8 r, g, b, a;
		SDL_GetRGBA(c, _inputFormat, &r, &g, &b, &a);

		if (a < 0) {
			return "___";
		}

		Input input = "";
		input += atomForValue(r);
		input += atomForValue(g);
		input += atomForValue(b);
		return input;
	}
	Color filterColor(Color c) {
		Uint8 r, g, b, a;
		SDL_GetRGBA(c, _inputFormat, &r, &g, &b, &a);
		if (!a) {
			return SDL_MapRGBA(_inputFormat, 0, 0, 0, 0);
		}

		if (!gArgs.useColor) {
			Uint8 o = (r + g + b) / 3;
			o = valueForAtom(atomForValue(o));
			return SDL_MapRGBA(_inputFormat, o, o, o, a);
		}
		else {
			return SDL_MapRGBA(_inputFormat,
				valueForAtom(atomForValue(r)),
				valueForAtom(atomForValue(g)),
				valueForAtom(atomForValue(b)),
				a);
		}
	}

	Color averageColors(Color c1, Color c2) {
		Uint8 r1, g1, b1, a1;
		Uint8 r2, g2, b2, a2;
		SDL_GetRGBA(c1, _inputFormat, &r1, &g1, &b1, &a1);
		SDL_GetRGBA(c2, _inputFormat, &r2, &g2, &b2, &a2);
		static const auto avg = [](Uint8 a, Uint8 b) {
			return (a + b) / 2;
		};
		return SDL_MapRGBA(_inputFormat, avg(r1, r2), avg(g1, g2), avg(b1, b2), avg(a1, a2));
	}

	void loadSurface(SDL_Surface *input) {
		if (!_initialized) {
			_bpp = input->format->BytesPerPixel;
			_inputFormat = input->format;
			_initialized = true;
		}

		auto rects = getSprites(input);

		auto getColor = [this, input](Point p) {
			return inputForColor(filterColor(getPixel(input, p.x, p.y)));
		};
		for (auto r : rects) {
			for (int y = r.y; y < r.y + r.h; ++y) {
				for (int x = r.x; x < r.x + r.w; ++x) {
					Input prev = getPrev(getColor, r, x, y);

					Color cur = filterColor(getPixel(input, x, y));
					_data[prev][cur] += 1;
				}
			}
		}

		for (auto inCount : _data) {
			int count = 0;
			for (auto colCount : inCount.second) {
				count += colCount.second + gArgs.biasTerm;
			}
			Probability probs;
			for (auto colCount : inCount.second) {
				probs[colCount.first] = (float)(colCount.second + gArgs.biasTerm) / count;
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
		_generatedPixels = new Uint8[width * height * _bpp];

		_width = width;
		_height = height;

		auto getColor = [this, width](Point p) {
			return inputForColor(getRawPixel(_generatedPixels, _bpp, _bpp * width, p.x, p.y));
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
			}
		}

		for (int i = 1; i < gArgs.passes; ++i) {
			Uint8 *nextPass = new Uint8[width * height * _bpp];
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					Input prev = getPrev(getColor, rect, x, y);
					Color cur = getRawPixel(_generatedPixels, _bpp, _bpp * width, x, y);
					Color curP2 = getNext(prev);
					Color avg = averageColors(cur, curP2);
					setRawPixel(nextPass, _bpp, _bpp * width, x, y, filterColor(avg));
				}
			}
			delete[] _generatedPixels;
			_generatedPixels = nextPass;
		}

		return _generatedPixels;
	}

	void PrintProbabilities() {
		for (auto inProb : _data) {
			cout << "In: " << inProb.first << '\n';
			for (auto colProb : inProb.second) {
				Uint8 r, g, b, a;
				SDL_GetRGBA(colProb.first, _inputFormat, &r, &g, &b, &a);
				printf("\t(%d, %d, %d) : %d\n", r, g, b, colProb.second);
			}
		}
	}

	bool WriteSurface(string filename) {
		auto surf = SDL_CreateRGBSurfaceFrom(_generatedPixels,
			_width, _height, _bpp * 8, _width * _bpp,
			_inputFormat->Rmask, _inputFormat->Gmask,
			_inputFormat->Bmask, _inputFormat->Amask);
		bool error = (SDL_SaveBMP(surf, filename.c_str()) == -1);
		SDL_FreeSurface(surf);
		return error;
	}

	SDL_PixelFormat* GetFormat() { return _inputFormat; }
};

int main(int argc, char** argv) {
	parseInput(argc, argv);

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		cout << "SDL could not initialize: Error: " << SDL_GetError() << endl;
		return 1;
	}
	int imgFlags = IMG_INIT_PNG;
	if (!(IMG_Init(imgFlags) & imgFlags)) {
		cout << "SDL_image could not initialize: Error: " << IMG_GetError() << endl;
		return 1;
	}
	int texSize = 16;

	SpriteMarkov markov;
	auto loadInDirectory = [&markov](string dir) {
		boost::filesystem::recursive_directory_iterator end;
		for (decltype(end) it(dir); it != end; ++it) {
			if (!is_directory(*it)) {
				auto path = it->path().string();
				cout << "Loading : " << path << '\n';
				markov.loadSurface(IMG_Load(path.c_str()));
			}
		}
	};
	loadInDirectory(gArgs.inputFolder);

	if (gArgs.printCounts) {
		markov.PrintProbabilities();
	}

	if (gArgs.outputFolder != "") {
		for (int i = 1; i <= gArgs.numOutputImages; ++i) {
			stringstream filenameStream;
			filenameStream << gArgs.outputFolder << i << ".bmp";
			string filename = filenameStream.str();

			cout << "Writing: " << filename << '\n';

			markov.CreatePixelData(texSize, texSize);
			if (markov.WriteSurface(filename.c_str())) {
				cout << "ERROR: could not write file \"" << filename
					<< "\" , SDL_Error: " << SDL_GetError() << endl;
				return 1;
			}
		}

		return 0;
	}

	int screenWidth = 800;
	int screenHeight = 600;
	auto window = SDL_CreateWindow("Procedural Sprites", 
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screenWidth, screenHeight, SDL_WINDOW_SHOWN);
	if (!window) {
		cout << "Window could not be created: Error: " << SDL_GetError() << endl;
		return 1;
	}
	auto renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (!renderer) {
		cout << "Renderer could not be created: Error: " << SDL_GetError() << endl;
		return 1;
	}

	SDL_Texture *texture = nullptr;
	auto rebuildTexture = [&texture, &texSize, &markov, renderer]() {
		if (texture) SDL_DestroyTexture(texture);
		texture = SDL_CreateTexture(renderer, markov.GetFormat()->format,
			SDL_TEXTUREACCESS_STATIC, texSize, texSize);
	};
	auto buildSprite = [&texture, &texSize, &markov](void* pixels) {
		SDL_UpdateTexture(texture, nullptr, pixels, texSize * markov.GetFormat()->BytesPerPixel);
	};
	auto remakeSprite = [&texture, &texSize, &markov, buildSprite]() {
		auto pixels = markov.CreatePixelData(texSize, texSize);
		buildSprite(pixels);
	};

	rebuildTexture();
	remakeSprite();

	bool quit = false;
	SDL_Event event;
	int inIndex = 0;
	const static float kScaleFactor = 1.2f;
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

				case SDLK_RIGHT:
				case SDLK_LEFT:
					remakeSprite();
					break;

				case SDLK_UP:
					texSize = (int)(texSize * kScaleFactor + 1);
					rebuildTexture();
					remakeSprite();
					break;
				case SDLK_DOWN:
					texSize = (int)(texSize / kScaleFactor - 1);
					texSize = SDL_max(texSize, 0);
					rebuildTexture();
					remakeSprite();
					break;
				}
			}
		}

		SDL_RenderClear(renderer);

		float scale = 20.0f;
		SDL_Rect rect;
		rect.w = (int)(scale * texSize);
		rect.h = (int)(scale * texSize);
		rect.x = (screenWidth - rect.w) / 2;
		rect.y = (screenHeight - rect.h) / 2;
		SDL_RenderCopy(renderer, texture, nullptr, &rect);

		SDL_RenderPresent(renderer);
	}

	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	// SDL_FreeSurface(inputSurface);

	IMG_Quit();
	SDL_Quit();

	return 0;
}

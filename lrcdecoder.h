#ifndef LRCDECODER_H
#define LRCDECODER_H

#include <vector>
#include <map>
#include <sstream>
#include <iostream>
#include <string>
#include <algorithm>
#include <regex>

typedef struct LyricWord {
	std::wstring word;
	int64_t pts1 = 0;
	int64_t pts2 = 0;
} LyricWord;

typedef struct LyricLine {
	std::wstring lyric;
	std::vector<LyricWord> words;
} LyricLine;

typedef struct LyricPacket {
	std::vector<LyricLine> lyrics;
	int64_t pts = 0;
	bool Empty();
} LyricPacket;

class LrcDecoderPrivate;
class LrcDecoder
{
public:
	LrcDecoder();
	~LrcDecoder();

	void Clear();
	bool Load(const std::wstring& lrcData);

	std::wstring GetMeta(const std::wstring &meta);
	LyricPacket ReadPacket(int index);

	int GetCount() const;
	int64_t GetDuration() const;

	std::wstring LastError() const;

private:
	LrcDecoderPrivate *d;
};

#endif // LRCDECODER_H

#include "lrcdecoder.h"

static const std::wstring MetaData[7][2] = {
	{L"ti", L"title"},
	{L"al", L"album"},
	{L"ar", L"artist"},
	{L"au", L"author"},
	{L"by", L"creator"},
	{L"re", L"encoder"},
	{L"ve", L"encoder_version"}
};

static std::wstring findMeta(const std::wstring &tag) {
	std::wstring data;
	for (int i = 0; i < 7; ++i) {
		if (tag == MetaData[i][0]) {
			data = MetaData[i][1];
			break;
		}
	}

	return data;
}

class LrcDecoderPrivate
{
public:
	int64_t m_duration = 0;
	size_t m_currentIndex = 0;
	int m_lrcCount;
	std::wstring m_lastError;
	std::wstring m_lrcData;
	std::map<std::wstring, std::wstring> m_metadata;
	std::vector<LyricPacket> m_lyrics;

	void cleanup();

	size_t decodeHeader();
	void decodeLine();
	void mergeLine();
};

LrcDecoder::LrcDecoder()
{
	d = new LrcDecoderPrivate;
}

LrcDecoder::~LrcDecoder()
{
	delete d;
}

void LrcDecoder::Clear()
{
	d->cleanup();
}

bool LrcDecoder::Load(const std::wstring& lrcData)
{
	d->cleanup();

	d->m_lrcData = lrcData;

	if (d->m_lrcData.empty()) {
		d->m_lastError = L"LRC file is empty!";
		return false;
	}

	size_t index = d->decodeHeader();

	if (index == d->m_lrcData.length()) {
		d->m_lastError = L"No lyrics text!";
		return false;
	}

	d->m_lrcData.erase(0, index);

	d->decodeLine();
	d->mergeLine();

	if (d->m_lyrics.empty()) return false;

	d->m_lrcCount = std::distance(d->m_lyrics.begin(), d->m_lyrics.end());
	d->m_duration = (--d->m_lyrics.end())->pts;

	return true;
}

std::wstring LrcDecoder::GetMeta(const std::wstring &meta)
{
	std::wstring data;
	if (d->m_metadata.find(meta) != d->m_metadata.end())
		data = d->m_metadata[meta];

	return data;
}

LyricPacket LrcDecoder::ReadPacket(int index)
{
	LyricPacket packet;
	if (!d->m_lyrics.empty())
	{
		packet = d->m_lyrics[index];
	}
	return packet;
}

int LrcDecoder::GetCount() const
{
	return d->m_lrcCount;
}

int64_t LrcDecoder::GetDuration() const
{
	return d->m_duration;
}

std::wstring LrcDecoder::LastError() const
{
	return d->m_lastError;
}

void LrcDecoderPrivate::cleanup()
{
	m_currentIndex = 0;
	m_lastError.clear();
	m_lrcData.clear();
	m_metadata.clear();
	m_lyrics.clear();
}

size_t LrcDecoderPrivate::decodeHeader()
{
	size_t offset = 0;
	size_t length = m_lrcData.length();

	if (offset >= length) return offset;

	while(offset < length) {
		std::wstring meta, data;
		if (m_lrcData.at(offset) == '[') {
			while(++offset < length && m_lrcData.at(offset) != ':') {
				if (m_lrcData.at(offset) >= 'a' && m_lrcData.at(offset) <= 'z')
					meta += m_lrcData.at(offset);
				else return offset - 1;
			}

			while(++offset < length && m_lrcData.at(offset) != ']') {
				data += m_lrcData.at(offset);
			}

			m_metadata[findMeta(meta)] = data;
		}

		offset++;
	}

	return offset;
}

/*
void LrcDecoderPrivate::decodeLine()
{
	std::wregex pattern(LR"(\[(\d{1,9}):(\d{1,2}).(\d{1,3})\](.*))");
	std::wsregex_iterator it(m_lrcData.begin(), m_lrcData.end(), pattern);
	std::wsregex_iterator end;

	std::wsmatch match;
	std::wstring lrc;
	std::wstring time;
	struct lrcdata 
	{
		std::wstring time;
		std::wstring lrc;
	};
	std::vector<lrcdata> data;

	while (it != end) {
		match = *it;
		time = match.str();
		++it;
		if (it != end) {
			match = *it;
		}
		lrc = match.prefix();
		data.push_back({ time, lrc });
	}
}
*/

void LrcDecoderPrivate::decodeLine()
{
	std::wregex patternTime(LR"(\[(\d{1,9}):(\d{1,2}).(\d{1,3})\])");
	std::wsregex_iterator itTime(m_lrcData.begin(), m_lrcData.end(), patternTime);
	std::wsregex_iterator endTime;
	std::wsmatch matchTime;
	if (itTime != endTime) matchTime = *itTime;
	std::wregex patternWTime(LR"(<(\d{1,9}):(\d{1,2}).(\d{1,3})>)");
	std::wsregex_iterator itWTime(m_lrcData.begin(), m_lrcData.end(), patternWTime);
	std::wsregex_iterator endWTime;
	std::wsmatch matchWTime;
	if (itWTime != endWTime) matchWTime = *itWTime;

	size_t length = m_lrcData.length();
	size_t offset = 0;
	std::wstring time;
	int64_t pts = 0;
	std::vector<int64_t> times;
	int64_t mult = 0;

	std::wstring lrc;

	int timeJoinState = 0; //0.none 1.begin 2.time1 3.colon 4.time2 5.point 6.time3
	int wtimeJoinState = 0; //0.none 1.begin 2.time1 3.colon 4.time2 5.point 6.time3

	bool wordJoin = false;
	int64_t wordPts1 = 0;
	int64_t wordPts2 = 0;

	int64_t packetPts = 0;

	LyricWord word;
	LyricLine line;
	LyricPacket packet;

	int addData = 0;

	while (offset <= length) {
		//if (offset == length ? true : (m_lrcData.at(offset) == '[')) {
		if (offset == length ? true : offset == matchTime.position()) {
			wtimeJoinState = 0;
			timeJoinState = 1;
			wordJoin = false;
			addData = 0;
			if (line.words.empty()) {
				line.lyric = lrc;
			}
			else {
				for (auto wi = line.words.begin(); wi < line.words.end(); wi++) {
					line.lyric += wi->word;
				}
			}
			packet.lyrics.push_back(line);
			packet.pts = packetPts;
			m_lyrics.push_back(packet);

			packet.lyrics.clear();
			line.words.clear();
			line.lyric.clear();
			pts = 0;
			lrc.clear();

			if (itTime != endTime) ++itTime;
			if (itTime != endTime) matchTime = *itTime;
		}
		//else if (m_lrcData.at(offset) == '<') {
		else if (offset == matchWTime.position()) {
			wtimeJoinState = 1;
			addData = 0;
			if (wordJoin) {
				word.word = lrc;
				lrc.clear();
			}
			if (itWTime != endWTime) ++itWTime;
			if (itWTime != endWTime) matchWTime = *itWTime;
		}
		else if (m_lrcData.at(offset) == ':' && (timeJoinState == 2 || wtimeJoinState == 2)) {
			//minute, = 60s * 1000ms
			if (!time.empty()) {
				if (timeJoinState == 2) timeJoinState = 3;
				if (wtimeJoinState == 2) wtimeJoinState = 3;
				pts += stoi(time) * 60 * 1000;
			}
			time.clear();
		}
		else if (m_lrcData.at(offset) == '.' && (timeJoinState == 4 || wtimeJoinState == 4)) {
			//second, = 1000 ms
			if (!time.empty()) {
				if (timeJoinState == 4) timeJoinState = 5;
				if (wtimeJoinState == 4) wtimeJoinState = 5;
				pts += stoi(time) * 1000;
			}
			time.clear();
		}
		else if (m_lrcData.at(offset) >= '0' && m_lrcData.at(offset) <= '9') {
			if (addData == 0) {
				if (timeJoinState == 1) timeJoinState = 2;
				if (timeJoinState == 3) timeJoinState = 4;
				if (timeJoinState == 5) timeJoinState = 6;
				if (wtimeJoinState == 1) wtimeJoinState = 2;
				if (wtimeJoinState == 3) wtimeJoinState = 4;
				if (wtimeJoinState == 5) wtimeJoinState = 6;
				time += m_lrcData.at(offset);
			}
		}
		else if (m_lrcData.at(offset) == '>' && wtimeJoinState == 6) {
			//10 millisecond
			mult = time.length() == 3 ? 1 : (3 - time.length()) * 10;
			if (!time.empty()) {
				pts += stoi(time) * mult;
			}
			if (wordJoin) {
				wordPts2 = pts;
			}
			else {
				wordPts1 = pts;
			}
			pts = 0;
			time.clear();
			if (wordJoin) {
				word.pts1 = wordPts1;
				word.pts2 = wordPts2;
				wordPts1 = 0;
				wordPts2 = 0;
				line.words.push_back(word);
				wordJoin = false;
			}
			else {
				wordJoin = true;
			}
			addData = wordJoin ? 1 : 0;
			wtimeJoinState = 0;
		}
		else if (m_lrcData.at(offset) == ']' && timeJoinState == 6) {
			//10 millisecond
			mult = time.length() == 3 ? 1 : (3 - time.length()) * 10;
			if (!time.empty()) {
				pts += stoi(time) * mult;
			}
			//times.push_back(pts);
			packetPts = pts;
			pts = 0;
			time.clear();
			addData = 1;
			timeJoinState = 0;
		}
		if (addData > 0) {
			if (addData == 2) {
				lrc += m_lrcData.at(offset);
			}
			else {
				addData++;
			}
		}

		m_currentIndex++;
		offset++;
	}
}

void LrcDecoderPrivate::mergeLine()
{
	if (m_lyrics.empty()) return;
	for (auto it1 = m_lyrics.begin() + 1; it1 < m_lyrics.end(); it1++)
	{
		for (auto it2 = m_lyrics.begin() + 1; it2 < m_lyrics.end(); it2++)
		{
			if (it2->pts == it1->pts && it2 != it1) {
				if (!it2->lyrics.empty()) {
					it1->lyrics.push_back(it2->lyrics[0]);
				}
				m_lyrics.erase(it2);
				break;
			}
		}
	}
}

bool LyricPacket::Empty()
{
	if (!lyrics.empty())
	{
		for (auto it = lyrics.begin(); it < lyrics.end(); it++)
		{
			if (!it->lyric.empty())
			{
				return false;
			}
		}
	}
	return true;
}
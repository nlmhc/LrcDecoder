#include "lrcdecoder.h"

class LrcDecoderPrivate
{
public:
	int64_t m_duration = 0;
	size_t m_currentIndex = 0;
	ptrdiff_t m_lrcCount;
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

std::wstring LrcDecoder::GetMeta(const std::wstring& meta)
{
	std::wstring data;
	if (d->m_metadata.find(meta) != d->m_metadata.end())
		data = d->m_metadata[meta];
	return data;
}

LyricPacket LrcDecoder::ReadPacket(int index)
{
	if (!d->m_lyrics.empty())
		return d->m_lyrics[index];
	return {};
}

ptrdiff_t LrcDecoder::GetCount() const
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
	size_t length = m_lrcData.size();

	if (offset >= length)
		return offset;

	while (offset < length) {
		std::wstring meta, data;
		if (m_lrcData[offset] == '[') {
			while (++offset < length && m_lrcData[offset] != ':') {
				if (m_lrcData[offset] >= 'a' && m_lrcData[offset] <= 'z' || m_lrcData[offset] >= 'A' && m_lrcData[offset] <= 'Z')
					meta += m_lrcData[offset];
				else
					return offset - 1;
			}

			while (++offset < length && m_lrcData[offset] != ']') {
				data += m_lrcData[offset];
			}

			m_metadata[meta] = data;
		}

		offset++;
	}

	return offset;
}

void LrcDecoderPrivate::decodeLine()
{
	bool begin = true;
	size_t length = m_lrcData.size();
	size_t offset = 0;
	std::wstring stime;
	std::wstring st_h, st_min, st_s, st_ms; // 时，分，秒，毫秒
	int64_t pts = 0;
	int64_t mult = 0;

	size_t timetagPos = 0;
	size_t wtimetagPos = 0;

	std::wstring lrc;
	bool nobeginspace = false;

	int countMultTimes = 1;
	std::vector<size_t> lrcMultTimes;

	enum class state {
		none,
		begin,
		time1,
		colon1,
		time2,
		colon2,
		time3,
		point,
		time4
	};

	state timeJoinState = state::none;
	state wtimeJoinState = state::none;

	bool wordJoin = false;
	int64_t wordPts1 = 0;
	int64_t wordPts2 = 0;
	int64_t wordPtsOld2 = 0;
	int64_t packetPts = 0;

	size_t wi = 0;
	size_t it = 0;

	LyricWord word{};
	LyricLine line{};
	LyricPacket packet{};

	int addData = 0;
	bool addDataIsWord = false;

	m_lyrics.reserve(length / 4);
	packet.lyrics.reserve(256);
	line.lyric.reserve(512);
	line.words.reserve(512);
	lrcMultTimes.reserve(512);
	lrc.reserve(512);
	stime.reserve(32);

	auto _findTimeTag = [](const std::wstring& str, bool angle, size_t begin = 0) {
		if (begin >= str.size())
			return std::wstring::npos;

		const auto lb = angle ? L'<' : L'[';
		const auto rb = angle ? L'>' : L']';

		for (size_t i = begin; i < str.size(); ++i) {
			if (str[i] != lb)
				continue;

			size_t end = str.find(rb, i + 1);
			if (end == std::wstring::npos)
				break;

			size_t a = i + 1, b = end - 1;
			if (a > b)
				continue;

			size_t lastColon = str.rfind(L':', b);
			if (lastColon == std::wstring::npos || lastColon < a)
				continue;

			bool ok = true;
			size_t pos = a;
			size_t colon = str.find(L':', pos);
			while (colon != std::wstring::npos && colon < lastColon) {
				size_t len = colon - pos;
				if (len < 1 || len > 9) {
					ok = false;
					break;
				}
				for (size_t t = pos; t < colon; ++t) {
					if (str[t] < L'0' || str[t] > L'9') {
						ok = false;
						break;
					}
				}
				if (!ok)
					break;
				pos = colon + 1;
				colon = str.find(L':', pos);
			}
			if (!ok)
				continue;

			size_t midLen = lastColon - pos;
			if (midLen < 1 || midLen > 9)
				continue;
			for (size_t t = pos; t < lastColon; ++t) {
				if (str[t] < L'0' || str[t] > L'9') {
					ok = false;
					break;
				}
			}
			if (!ok)
				continue;

			size_t lastStart = lastColon + 1;
			size_t lastLen = b - lastStart + 1;
			if (lastLen < 1)
				continue;

			size_t dot = str.find(L'.', lastStart);
			size_t firstPartLen = (dot == std::wstring::npos || dot > b) ? lastLen : dot - lastStart;
			if (firstPartLen < 1 || firstPartLen > 2)
				continue;
			for (size_t t = lastStart; t < lastStart + firstPartLen; ++t) {
				if (str[t] < L'0' || str[t] > L'9') {
					ok = false;
					break;
				}
			}
			if (!ok)
				continue;

			size_t cur = dot;
			while (cur != std::wstring::npos && cur <= b) {
				size_t nextDot = str.find(L'.', cur + 1);
				size_t partEnd = (nextDot == std::wstring::npos || nextDot > b) ? b + 1 : nextDot;
				size_t partLen = partEnd - (cur + 1);
				if (partLen < 1 || partLen > 3) {
					ok = false;
					break;
				}
				for (size_t t = cur + 1; t < partEnd; ++t) {
					if (str[t] < L'0' || str[t] > L'9') {
						ok = false;
						break;
					}
				}
				if (!ok)
					break;
				cur = nextDot;
			}
			if (ok)
				return i;
		}
		return std::wstring::npos;
		};

	auto _st2pts = [&]() {
		int64_t _pts = 0;
		//毫秒
		if (!st_ms.empty()) {
			mult = pow(10, 3 - st_ms.length());
			_pts += std::stoll(st_ms) * mult;
		}
		//秒, = 1000 ms
		if (!st_s.empty()) {
			_pts += std::stoll(st_s) * 1000;
		}
		//分钟, = 60s * 1000ms
		if (!st_min.empty()) {
			_pts += std::stoll(st_min) * 60 * 1000;
		}
		//小时, = 60min * 60s * 1000ms
		if (!st_h.empty()) {
			_pts += std::stoll(st_h) * 60 * 60 * 1000;
		}
		return _pts;
		};

	timetagPos = _findTimeTag(m_lrcData, false);
	wtimetagPos = _findTimeTag(m_lrcData, true);
	while (offset <= length) {
		if (offset == length ? true : offset == timetagPos) {
			wtimeJoinState = state::none;
			timeJoinState = state::begin;
			wordJoin = false;
			addData = 0;

			if (!begin) {
				if (line.words.empty()) {
					line.lyric = lrc;
				}
				else {
					for (wi = 0; wi < line.words.size(); wi++) {
						line.lyric.append(line.words[wi].word);
					}
				}
				packet.lyrics.emplace_back(line);
				packet.pts = packetPts;
				m_lyrics.emplace_back(packet);
				lrcMultTimes.emplace_back(m_lyrics.size() - 1);
				if (countMultTimes == 0 && !lrcMultTimes.empty()) {
					for (it = 0; it < lrcMultTimes.size(); it++) {
						m_lyrics[lrcMultTimes[it]].lyrics = m_lyrics[lrcMultTimes[lrcMultTimes.size() - 1]].lyrics;
					}
					lrcMultTimes.clear();
				}
				countMultTimes++;
			}
			else {
				packet.lyrics.emplace_back(line);
				packet.pts = 0;
				m_lyrics.emplace_back(packet);
			}

			packet.lyrics.clear();
			line.words.clear();
			line.lyric.clear();
			pts = 0;
			lrc = L"";
			nobeginspace = false;

			timetagPos = _findTimeTag(m_lrcData, false, timetagPos + 1);

			begin = false;
		}
		else if (offset == wtimetagPos) {
			wtimeJoinState = state::begin;
			addData = 0;
			countMultTimes = 0;
			if (wordJoin) {
				word.word = lrc;
				lrc = L"";
				//nobeginspace = false;
			}
			wtimetagPos = _findTimeTag(m_lrcData, true, wtimetagPos + 1);
		}
		else if (m_lrcData[offset] == ':' && (timeJoinState == state::time1 || wtimeJoinState == state::time1)) {
			st_h = stime;
			if (timeJoinState == state::time1) timeJoinState = state::colon1;
			if (wtimeJoinState == state::time1) wtimeJoinState = state::colon1;
			stime = L"";
		}
		else if (m_lrcData[offset] == ':' && (timeJoinState == state::time2 || wtimeJoinState == state::time2)) {
			st_min = stime;
			if (timeJoinState == state::time2) timeJoinState = state::colon2;
			if (wtimeJoinState == state::time2) wtimeJoinState = state::colon2;
			stime = L"";
		}
		else if (m_lrcData[offset] == '.' && (timeJoinState >= state::time2 || wtimeJoinState >= state::time2)) {
			if (timeJoinState == state::time2 || wtimeJoinState == state::time2) {
				st_s = stime;
				st_min = st_h;
				st_h = L"";
			}
			else if (timeJoinState == state::time3 || wtimeJoinState == state::time3) {
				st_s = stime;
			}
			if (timeJoinState >= state::time2) timeJoinState = state::point;
			if (wtimeJoinState >= state::time2) wtimeJoinState = state::point;
			stime = L"";
		}
		else if (m_lrcData[offset] >= '0' && m_lrcData[offset] <= '9') {
			if (addData == 0) {
				if (timeJoinState == state::begin) timeJoinState = state::time1;
				if (timeJoinState == state::colon1) timeJoinState = state::time2;
				if (timeJoinState == state::colon2) timeJoinState = state::time3;
				if (timeJoinState == state::point) timeJoinState = state::time4;
				if (wtimeJoinState == state::begin) wtimeJoinState = state::time1;
				if (wtimeJoinState == state::colon1) wtimeJoinState = state::time2;
				if (wtimeJoinState == state::colon2) wtimeJoinState = state::time3;
				if (wtimeJoinState == state::point) wtimeJoinState = state::time4;
				stime.append(1, m_lrcData[offset]);
			}
		}
		else if (m_lrcData[offset] == '>' && wtimeJoinState >= state::time2) {
			if (wtimeJoinState == state::time4)
				st_ms = stime;
			else if (wtimeJoinState == state::time3) {
				st_ms = stime;
				st_s = st_min;
				st_min = st_h;
				st_h = L"";
			}
			else if (wtimeJoinState == state::time2) {
				st_s = stime;
				st_min = st_h;
				st_h = L"";
			}
			stime = L"";
			pts = _st2pts();

			if (wordJoin) {
				wordPts2 = pts;
			}
			else {
				wordPts1 = pts;
			}
			pts = 0;
			if (wordJoin) {
				bool singlewtime = false;
				if (offset + 1 < length)
					singlewtime = m_lrcData[offset + 1] != '<';
				word.pts1 = (singlewtime && !line.words.empty()) || wordPts1 == 0 ? wordPtsOld2 : wordPts1;
				word.pts2 = wordPts2;
				wordPts1 = 0;
				wordPts2 = 0;
				wordPtsOld2 = word.pts2;
				line.words.emplace_back(word);
				wordJoin = singlewtime;
			}
			else {
				wordJoin = true;
			}
			addData = 1;
			addDataIsWord = true;
			wtimeJoinState = state::none;
		}
		else if (m_lrcData[offset] == ']' && timeJoinState >= state::time2) {
			if (timeJoinState == state::time4)
				st_ms = stime;
			else if (timeJoinState == state::time3) {
				st_ms = stime;
				st_s = st_min;
				st_min = st_h;
				st_h = L"";
			}
			else if (timeJoinState == state::time2) {
				st_s = stime;
				st_min = st_h;
				st_h = L"";
			}
			stime = L"";
			pts = _st2pts();

			packetPts = pts;
			wordPtsOld2 = packetPts;
			pts = 0;
			addData = 1;
			addDataIsWord = false;
			timeJoinState = state::none;
		}
		if (addData == 1 && timeJoinState == state::none && wtimeJoinState == state::none) {
			if (addDataIsWord) {
				if (wordJoin)
					addData = 2;
			}
			else {
				if (offset + 1 != timetagPos)
					addData = 2;
			}
		}
		if (addData > 1) {
			countMultTimes = 0;
			if (addData == 3) {
				const auto& ld = m_lrcData[offset];
				if (!nobeginspace && ld != ' ' && ld != '\t' && ld != '\v' && ld != '\f')
					nobeginspace = true;
				if (nobeginspace && ld != '\n' && ld != '\r') {
					lrc.append(1, ld);
				}
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
	size_t it1 = 0;
	size_t it2 = 0;
	size_t itlrc = 0;
	if (m_lyrics.empty()) return;
	for (it1 = 1; it1 < m_lyrics.size(); it1++) {
		for (it2 = 1; it2 < m_lyrics.size(); it2++) {
			if (m_lyrics[it2].pts == m_lyrics[it1].pts && it2 != it1) {
				for (itlrc = 0; itlrc < m_lyrics[it2].lyrics.size(); itlrc++) {
					m_lyrics[it1].lyrics.emplace_back(m_lyrics[it2].lyrics[itlrc]);
				}
				m_lyrics.erase(m_lyrics.begin() + it2);
				it2 = 1;
			}
		}
	}
	std::sort(m_lyrics.begin(), m_lyrics.end(), [](const LyricPacket& p1, const LyricPacket& p2) {
		return p1.pts < p2.pts;
		});
}

bool LyricPacket::Empty()
{
	if (!lyrics.empty()) {
		for (size_t it = 0; it < lyrics.size(); it++) {
			if (!lyrics[it].lyric.empty()) return false;
		}
	}
	return true;
}
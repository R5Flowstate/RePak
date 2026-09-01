//=============================================================================//
//
// purpose: various common utilities
//
//=============================================================================//
#include "pch.h"
#include "utils.h"
#include "rapidjson/error/en.h"
#define NOMINMAX
#include <windows.h>
#include <limits>
#include <cctype>
#include <limits>

namespace
{
constexpr uint64_t kFileTimeTicksPerSecond = 10000000ULL;
constexpr uint64_t kUnixEpochToFileTimeOffset = 116444736000000000ULL;
constexpr uint64_t kMaxUnixSeconds = (0xFFFFFFFFFFFFFFFFULL - kUnixEpochToFileTimeOffset) / kFileTimeTicksPerSecond;

bool ConvertUnixSecondsToFileTime(const uint64_t seconds, FILETIME& outFileTime)
{
    if (seconds > kMaxUnixSeconds)
        return false;

    const uint64_t fileTimeTicks = seconds * kFileTimeTicksPerSecond + kUnixEpochToFileTimeOffset;
    outFileTime.dwLowDateTime = static_cast<DWORD>(fileTimeTicks);
    outFileTime.dwHighDateTime = static_cast<DWORD>(fileTimeTicks >> 32);
    return true;
}

bool ParseFixedWidthInt(const char* const str, const size_t len, const size_t offset, const size_t width,
    const int minValue, const int maxValue, int& outValue)
{
    if ((offset + width) > len)
        return false;

    int value = 0;
    for (size_t i = 0; i < width; ++i)
    {
        const char c = str[offset + i];
        if (c < '0' || c > '9')
            return false;

        value = (value * 10) + (c - '0');
    }

    if (value < minValue || value > maxValue)
        return false;

    outValue = value;
    return true;
}
}

//-----------------------------------------------------------------------------
// purpose: gets size of the specified file
// returns: file size
//-----------------------------------------------------------------------------
uintmax_t Utils::GetFileSize(const std::string& filename) // !TODO: change to 'fs::path' instead?
{
	try {
		return std::filesystem::file_size(filename);
	}
	catch (std::filesystem::filesystem_error& e) {
		std::cout << e.what() << '\n';
		exit(0);
	}
}

//-----------------------------------------------------------------------------
// purpose: pad buffer to the specified alignment
// returns: new buffer size
//-----------------------------------------------------------------------------
size_t Utils::PadBuffer(char** buf, size_t size, size_t alignment)
{
	size_t newSize = IALIGN(size, alignment);

	char* newbuf = new char[newSize]{};
	memcpy_s(newbuf, size, *buf, size);

	delete[] *buf;

	*buf = newbuf;
	return newSize;
}

//-----------------------------------------------------------------------------
// purpose: write vector of strings to the specified BinaryIO instance
// returns: length of data written
//-----------------------------------------------------------------------------
size_t Utils::WriteStringVector(BinaryIO& out, const std::vector<std::string>& dataVector)
{
	size_t lenTotal = 0;
	for (auto it = dataVector.begin(); it != dataVector.end(); ++it)
	{
		// NOTE: +1 because we need to take the null char into account too.
		const size_t lenPath = it->length() + 1;
		lenTotal += lenPath;

		out.Write(it->c_str(), lenPath);
	}
	return lenTotal;
}

//-----------------------------------------------------------------------------
// purpose: get current system time as FILETIME
//-----------------------------------------------------------------------------
FILETIME Utils::GetSystemFileTime()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return ft;
}

bool Utils::TryParseUnixTimestampToFileTime(int64_t seconds, FILETIME& outFileTime)
{
    if (seconds < 0)
        return false;

    return ConvertUnixSecondsToFileTime(static_cast<uint64_t>(seconds), outFileTime);
}

bool Utils::TryParseUnixTimestampToFileTime(uint64_t seconds, FILETIME& outFileTime)
{
    return ConvertUnixSecondsToFileTime(seconds, outFileTime);
}

bool Utils::TryParseIso8601UtcToFileTime(const char* const value, const size_t length, FILETIME& outFileTime)
{
    if (!value || length == 0)
        return false;

    size_t begin = 0;
    size_t end = length;

    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;

    if ((end - begin) < 10)
        return false;

    const char* const trimmed = value + begin;
    const size_t trimmedLength = end - begin;

    if (trimmedLength < 10 || trimmed[4] != '-' || trimmed[7] != '-')
        return false;

    int year;
    int month;
    int day;

    if (!ParseFixedWidthInt(trimmed, trimmedLength, 0, 4, 1601, 30827, year))
        return false;
    if (!ParseFixedWidthInt(trimmed, trimmedLength, 5, 2, 1, 12, month))
        return false;
    if (!ParseFixedWidthInt(trimmed, trimmedLength, 8, 2, 1, 31, day))
        return false;

    int hour = 0;
    int minute = 0;
    int second = 0;
    int tzOffsetMinutes = 0;
    size_t pos = 10;

    if (trimmedLength > pos)
    {
        const char separator = trimmed[pos];
        if (separator != 'T' && separator != 't' && separator != ' ')
            return false;
        ++pos;

        if (!ParseFixedWidthInt(trimmed, trimmedLength, pos, 2, 0, 23, hour))
            return false;
        pos += 2;

        if (pos >= trimmedLength || trimmed[pos] != ':')
            return false;
        ++pos;

        if (!ParseFixedWidthInt(trimmed, trimmedLength, pos, 2, 0, 59, minute))
            return false;
        pos += 2;

        if (pos < trimmedLength && trimmed[pos] == ':')
        {
            ++pos;
            if (!ParseFixedWidthInt(trimmed, trimmedLength, pos, 2, 0, 59, second))
                return false;
            pos += 2;
        }

        if (pos < trimmedLength && trimmed[pos] == '.')
        {
            ++pos;
            while (pos < trimmedLength && std::isdigit(static_cast<unsigned char>(trimmed[pos])))
                ++pos;
        }

        if (pos < trimmedLength)
        {
            const char tz = trimmed[pos];
            if (tz == 'Z' || tz == 'z')
            {
                ++pos;
            }
            else if (tz == '+' || tz == '-')
            {
                const int sign = (tz == '+') ? 1 : -1;
                ++pos;

                int tzHour;
                if (!ParseFixedWidthInt(trimmed, trimmedLength, pos, 2, 0, 23, tzHour))
                    return false;
                pos += 2;

                if (pos < trimmedLength && trimmed[pos] == ':')
                    ++pos;

                int tzMinute;
                if (!ParseFixedWidthInt(trimmed, trimmedLength, pos, 2, 0, 59, tzMinute))
                    return false;
                pos += 2;

                tzOffsetMinutes = sign * (tzHour * 60 + tzMinute);
            }
            else
            {
                return false;
            }

            if (pos != trimmedLength)
                return false;
        }
    }

    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(year);
    st.wMonth = static_cast<WORD>(month);
    st.wDay = static_cast<WORD>(day);
    st.wHour = static_cast<WORD>(hour);
    st.wMinute = static_cast<WORD>(minute);
    st.wSecond = static_cast<WORD>(second);
    st.wMilliseconds = 0;

    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft))
        return false;

    if (tzOffsetMinutes != 0)
    {
        ULARGE_INTEGER ticks;
        ticks.HighPart = ft.dwHighDateTime;
        ticks.LowPart = ft.dwLowDateTime;

        int64_t signedTicks = static_cast<int64_t>(ticks.QuadPart);
        signedTicks -= static_cast<int64_t>(tzOffsetMinutes) * 60LL * static_cast<int64_t>(kFileTimeTicksPerSecond);

        if (signedTicks < 0)
            return false;

        ticks.QuadPart = static_cast<uint64_t>(signedTicks);
        ft.dwLowDateTime = ticks.LowPart;
        ft.dwHighDateTime = ticks.HighPart;
    }

    outFileTime = ft;
    return true;
}

//-----------------------------------------------------------------------------
// purpose: add backslash to the end of the string if not already present
//-----------------------------------------------------------------------------
void Utils::AppendSlash(std::string& in)
{
	const char lchar = in[in.size() - 1];
	if (lchar != '\\' && lchar != '/')
		in.append("\\");
}

//-----------------------------------------------------------------------------
// purpose: normalizes the slash directions inside a given string
//-----------------------------------------------------------------------------
void Utils::FixSlashes(std::string& in, const char correctPathSeparator)
{
    for (char& lchar : in)
    {
        if (lchar == '\\' || lchar == '/')
            lchar = correctPathSeparator;
    }
}

//-----------------------------------------------------------------------------
// purpose: replace extension with that of a new one in string
//-----------------------------------------------------------------------------
std::string Utils::ChangeExtension(const std::string& in, const std::string& ext)
{
	// Fast path for simple cases - avoid std::filesystem overhead
	const size_t dotPos = in.find_last_of('.');
	const size_t slashPos = in.find_last_of("/\\");

	// Ensure extension starts with a dot (like std::filesystem::replace_extension does)
	const std::string dotExt = ext.empty() ? "" : (ext[0] == '.' ? ext : '.' + ext);

	// Only replace if dot is after the last slash (or if no slash exists)
	if (dotPos != std::string::npos && (slashPos == std::string::npos || dotPos > slashPos))
	{
		std::string result = in;
		result.resize(dotPos);
		result += dotExt;
		return result;
	}

	// No extension found, just append
	return in + dotExt;
}

void Utils::ResolvePath(std::string& outPath, const std::filesystem::path& mapPath)
{
    fs::path outputDirPath(outPath);

    if (outputDirPath.is_relative() && mapPath.has_parent_path())
    {
        try {
            outPath = fs::canonical(mapPath.parent_path() / outputDirPath).string();
        }
        catch (const fs::filesystem_error& e) {
            Error("Failed to resolve path \"%s\": %s.\n", mapPath.string().c_str(), e.what());
        }
    }
    // else we just use whatever is in outPath.

    if (!strrchr(outPath.c_str(), '.'))
    {
        // ensure that the path has a slash at the end
        Utils::AppendSlash(outPath);
    }
}

const char* Utils::ExtractFileName(const std::string& inPath)
{
    const size_t len = inPath.length();
    const char* result = nullptr;

    for (size_t i = (len - 1); i-- > 0;)
    {
        const char c = inPath[i];

        if (c == '/' || c == '\\')
        {
            result = &inPath[i] + 1; // +1 to advance from slash.
            break;
        }
    }

    // No path, this is already the file name.
    if (!result)
        result = inPath.c_str();

    return result;
}

//-----------------------------------------------------------------------------
// purpose: prints a progress bar to console
//-----------------------------------------------------------------------------
void Utils::ProgressPrint(size_t current, size_t total, const char* const label)
{
    if (total == 0)
        return;

    const int barWidth = 30;
    const float progress = (float)current / total;
    const int pos = (int)(barWidth * progress);

    printf("\r%s[", label);
    for (int i = 0; i < barWidth; i++)
        printf(i < pos ? "=" : (i == pos ? ">" : " "));
    printf("] %d%% (%zu/%zu)", (int)(progress * 100), current, total);
    fflush(stdout);
}

//-----------------------------------------------------------------------------
// purpose: completes the progress bar with a newline
//-----------------------------------------------------------------------------
void Utils::ProgressComplete()
{
    printf("\n");
    fflush(stdout);
}

bool Util_ReplaceStream(BinaryIO& mainStream, BinaryIO& toSwap, const char* const mainPath, const char* const toSwapPath)
{
    toSwap.Close();
    mainStream.Close();

    // note(amos): we must reopen the file in ReadWrite mode as otherwise
    // the file gets truncated.

    if (!std::filesystem::remove(mainPath))
    {
        Warning("%s: failed to remove file \"%s\" for swap.\n", __FUNCTION__, mainPath);

        // reopen and continue uncompressed.
        if (mainStream.Open(mainPath, BinaryIO::Mode_e::ReadWrite))
            Error("%s: failed to reopen file \"%s\".\n", __FUNCTION__, mainPath);

        return false;
    }

    std::filesystem::rename(toSwapPath, mainPath);

    // either the rename failed or something holds an open handle to the
    // newly renamed compressed file, irrecoverable.
    if (!mainStream.Open(mainPath, BinaryIO::Mode_e::ReadWrite))
        Error("%s: failed to reopen file \"%s\".\n", __FUNCTION__, mainPath);

    return true;
}

PakGuid_t Pak_ParseGuid(const rapidjson::Value& val, bool* const success)
{
    PakGuid_t guid;

    // Try parsing it out from number
    if (JSON_ParseNumber(val, guid))
    {
        if (success) *success = true;
        return guid;
    }

    // Parse it from string
    if (val.IsString())
    {
        if (success) *success = true;
        return RTech::StringToGuid(val.GetString());
    }

    if (success) *success = false;
    return 0;
}

PakGuid_t Pak_ParseGuid(const rapidjson::Value& val, rapidjson::Value::StringRefType member, bool* const success)
{
    rapidjson::Value::ConstMemberIterator it;

    if (JSON_GetIterator(val, member, it))
        return Pak_ParseGuid(it->value, success);

    if (success) *success = false;
    return 0;
}

PakGuid_t Pak_ParseGuidDefault(const rapidjson::Value& val, rapidjson::Value::StringRefType member, const PakGuid_t fallback)
{
    bool success;
    const PakGuid_t guid = Pak_ParseGuid(val, member, &success);

    if (success)
        return guid;

    return fallback;
}

PakGuid_t Pak_ParseGuidDefault(const rapidjson::Value& val, rapidjson::Value::StringRefType member, const char* const fallback)
{
    bool success;
    const PakGuid_t guid = Pak_ParseGuid(val, member, &success);

    if (success)
        return guid;

    return RTech::StringToGuid(fallback);
}

PakGuid_t Pak_ParseGuidRequired(const rapidjson::Value& val, rapidjson::Value::StringRefType member)
{
    bool success;
    const PakGuid_t guid = Pak_ParseGuid(val, member, &success);

    if (!success)
        Error("%s: failed to parse field \"%s\".\n", __FUNCTION__, member.s);

    return guid;
}

//-----------------------------------------------------------------------------
// purpose: check if we have an override guid, and return that, else compute it
//          from the given asset path.
// NOTE   : this should be the only function used to get guids for asset entries
//-----------------------------------------------------------------------------
PakGuid_t Pak_GetGuidOverridable(const rapidjson::Value& mapEntry, const char* const assetPath)
{
    PakGuid_t assetGuid;

    if (JSON_ParseNumber(mapEntry, "$guid", assetGuid))
    {
        if (assetGuid == 0)
            Error("%s: invalid GUID override provided for asset \"%s\".\n", __FUNCTION__, assetPath);

        return assetGuid;
    }

    return RTech::StringToGuid(assetPath);
}

// If the field was defined as a string, outAssetName will point to the asset's name
PakGuid_t Pak_ParseGuidFromObject(const rapidjson::Value& val, const char* const debugName,
    const char*& outAssetName)
{
    PakGuid_t resultGuid;

    if (JSON_ParseNumber(val, resultGuid))
        return resultGuid;

    if (!val.IsString())
        Error("%s: %s is of unsupported type; expected %s or %s, found %s.\n", __FUNCTION__, debugName,
            JSON_TypeToString(JSONFieldType_e::kUint64), JSON_TypeToString(JSONFieldType_e::kString),
            JSON_TypeToString(JSON_ExtractType(val)));

    if (val.GetStringLength() == 0)
        Error("%s: %s was defined as an invalid empty string.\n", __FUNCTION__, debugName);

    outAssetName = val.GetString();
    return RTech::StringToGuid(outAssetName);
}

PakGuid_t Pak_ParseGuidFromMap(const rapidjson::Value& mapEntry, rapidjson::Value::StringRefType fieldName,
    const char* const debugName, const char*& outAssetName, const bool requiredField)
{
    rapidjson::Value::ConstMemberIterator it;

    if (requiredField)
        JSON_GetRequired(mapEntry, fieldName, it);
    else
    {
        if (!JSON_GetIterator(mapEntry, fieldName, it))
            return 0;
    }

    return Pak_ParseGuidFromObject(it->value, debugName, outAssetName);
}

size_t Pak_ExtractAssetStem(const char* const assetPath, char* const outBuf, const size_t outBufLen, const char* const assetPrefix)
{
    const char* bufPos = assetPath;

    // check if the path has the desired prefix
    if (strstr(assetPath, assetPrefix))
    {
        const size_t prefixLength = strnlen(assetPrefix, 32ull);

        assert(bufPos[prefixLength] == '/' || bufPos[prefixLength] == '\\');

        // add one to skip the (back)slash
        bufPos += (prefixLength + 1);
    }

    // copy until '.rpak' or '\0'
    size_t i = 0;
    while (*bufPos != '\0' && *bufPos != '.')
    {
        if (i == outBufLen)
            Error("%s: ran out of space on %s.\n", __FUNCTION__, assetPath);

        outBuf[i++] = *bufPos;
        bufPos++;
    }

    outBuf[i] = '\0';
    return i;
}


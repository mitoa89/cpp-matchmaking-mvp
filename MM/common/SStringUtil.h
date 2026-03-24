#pragma once
#pragma once

#include <algorithm>
#include <cerrno>
#include <clocale>
#include <cstdlib>
#include <cwchar>
#if defined(_WIN32)
#include <windows.h>
#endif
#if !defined(_WIN32)
#include <iconv.h>
#endif
#include <string>
#include "SStringFormat.h"

#ifndef OUT
#define OUT
#endif

// 문자열 변환 유틸리티
class SStringUtil
{
private:
	static void EnsureUtf8Locale()
	{
		static bool locale_initialized = []()
		{
			std::setlocale(LC_ALL, "");
			return true;
		}();
		(void)locale_initialized;
	}

#if defined(_WIN32)
	static std::string NarrowFromWide(const std::wstring& str)
	{
		if (str.empty())
			return {};

		const auto required = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0, nullptr, nullptr);
		if (required <= 0)
			return {};

		std::string out_str(static_cast<size_t>(required), '\0');
		WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), out_str.data(), required, nullptr, nullptr);
		return out_str;
	}

	static std::wstring WideFromNarrow(const std::string& str)
	{
		if (str.empty())
			return {};

		const auto required = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
		if (required <= 0)
			return {};

		std::wstring out_str(static_cast<size_t>(required), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), out_str.data(), required);
		return out_str;
	}
#else
	static std::string ConvertWithIconv(const char* from_encoding, const char* to_encoding, const char* input_bytes, size_t input_size)
	{
		if (input_bytes == nullptr || input_size == 0)
			return {};

		iconv_t cd = iconv_open(to_encoding, from_encoding);
		if (cd == reinterpret_cast<iconv_t>(-1))
			return {};

		std::string output;
		char* in_buf = const_cast<char*>(input_bytes);
		size_t in_bytes_left = input_size;

		while (in_bytes_left > 0)
		{
			char buffer[512] {};
			char* out_buf = buffer;
			size_t out_bytes_left = sizeof(buffer);

			const size_t result = iconv(cd, &in_buf, &in_bytes_left, &out_buf, &out_bytes_left);
			output.append(buffer, sizeof(buffer) - out_bytes_left);

			if (result != static_cast<size_t>(-1))
				continue;

			if (errno == E2BIG)
				continue;

			if (errno == EINVAL || errno == EILSEQ)
			{
				iconv_close(cd);
				return {};
			}
		}

		char* flush_in = nullptr;
		size_t flush_in_bytes = 0;
		while (true)
		{
			char buffer[512] {};
			char* out_buf = buffer;
			size_t out_bytes_left = sizeof(buffer);
			const size_t result = iconv(cd, &flush_in, &flush_in_bytes, &out_buf, &out_bytes_left);
			output.append(buffer, sizeof(buffer) - out_bytes_left);
			if (result != static_cast<size_t>(-1))
				break;
			if (errno != E2BIG)
				break;
		}

		iconv_close(cd);
		return output;
	}

	static std::string NarrowFromWide(const std::wstring& str)
	{
		return ConvertWithIconv("WCHAR_T", "UTF-8", reinterpret_cast<const char*>(str.data()), str.size() * sizeof(wchar_t));
	}

	static std::wstring WideFromNarrow(const std::string& str)
	{
		const std::string bytes = ConvertWithIconv("UTF-8", "WCHAR_T", str.data(), str.size());
		if (bytes.empty())
			return {};

		return std::wstring(reinterpret_cast<const wchar_t*>(bytes.data()), bytes.size() / sizeof(wchar_t));
	}
#endif

public:
	static std::string ToString(const std::wstring& str)
	{
		EnsureUtf8Locale();
		return NarrowFromWide(str);
	}
	static void ToString(const std::wstring& str, OUT std::string& out_str)
	{
		out_str = ToString(str);
	}

public:
	static std::wstring ToWide(const std::string& str)
	{
		EnsureUtf8Locale();
		return WideFromNarrow(str);
	}
	static void ToWide(const std::string& str, OUT std::wstring& out_str)
	{
		out_str = ToWide(str);
	}

public:
	static std::wstring UnicodeToWide(const std::string& str)
	{
		return UnicodeToWide(str.c_str());
	}
	static std::wstring UnicodeToWide(const char* str)
	{
		return ToWide(str == nullptr ? std::string() : std::string(str));
	}
	static void UnicodeToWide(const std::string& str, OUT std::wstring& out_str)
	{
		UnicodeToWide(str.c_str(), out_str);
		return;
	}
	static void UnicodeToWide(const char* str, OUT std::wstring& out_str)
	{
		out_str = UnicodeToWide(str);
	}
public:
	static std::string WideToUnicode(const std::wstring& str)
	{
		return WideToUnicode(str.c_str());
	}
	static std::string WideToUnicode(const wchar_t* str)
	{
		return ToString(str == nullptr ? std::wstring() : std::wstring(str));
	}
	static void WideToUnicode(const std::wstring& str, OUT std::string& out_str)
	{
		WideToUnicode(str.c_str(), out_str);
		return;
	}
	static void WideToUnicode(const wchar_t* str, OUT std::string& out_str)
	{
		out_str = WideToUnicode(str);
	}

public:
	template <typename TyName>
	static typename std::enable_if<std::is_arithmetic<TyName>::value&& std::is_floating_point<TyName>::value,
		TyName>::type StringTo(const std::string& value)
	{
		try
		{
			return static_cast<TyName>(std::stod(value));
		}
		catch (...)
		{
			return static_cast<TyName>(0.0);
		}
	}
	template <typename TyName>
	static typename std::enable_if<std::is_arithmetic<TyName>::value&& std::is_floating_point<TyName>::value,
		TyName>::type StringTo(const std::wstring& value)
	{
		try
		{
			return static_cast<TyName>(std::stod(value));
		}
		catch (...)
		{
			return static_cast<TyName>(0);
		}
	}
	template <typename TyName>
	static typename std::enable_if<std::is_arithmetic<TyName>::value&& std::is_integral<TyName>::value,
		TyName>::type StringTo(const std::string& value)
	{
		try
		{
			return static_cast<TyName>(std::stoll(value));
		}
		catch (...)
		{
			return static_cast<TyName>(0.0);
		}
	}
	template <typename TyName>
	static typename std::enable_if<std::is_arithmetic<TyName>::value&& std::is_integral<TyName>::value,
		TyName>::type StringTo(const std::wstring& value)
	{
		try
		{
			return static_cast<TyName>(std::stoll(value));
		}
		catch (...)
		{
			return static_cast<TyName>(0);
		}
	}

public:
	static std::wstring DoubleToWide(double value)
	{
		std::wstring str;

		DoubleToWide(value, str);

		return str;
	}
	static void DoubleToWide(double value, OUT std::wstring& out_str)
	{
		stdutil::format(out_str, L"%.3f", value);
		return;
	}
	static std::string DoubleToString(double value)
	{
		std::string str;

		DoubleToString(value, str);

		return str;
	}
	static void DoubleToString(double value, OUT std::string& out_str)
	{
		stdutil::format(out_str, "%.3f", value);
		return;
	}

public:
	static std::wstring IntToWide(long long value)
	{
		std::wstring str;

		IntToWide(value, str);

		return str;
	}
	static void IntToWide(long long value, OUT std::wstring& out_str)
	{
		stdutil::format(out_str, L"%LLd", value);

		return;
	}
	static std::string IntToString(long long value)
	{
		std::string str;

		IntToString(value, str);

		return str;
	}
	static void IntToString(long long value, OUT std::string& out_str)
	{
		stdutil::format(out_str, "%LLd", value);
		return;
	}

public:
	static std::wstring UIntToWide(unsigned long long value)
	{
		std::wstring str;

		UIntToWide(value, str);

		return str;
	}
	static void UIntToWide(unsigned long long value, OUT std::wstring& out_str)
	{
		stdutil::format(out_str, L"%LLu", value);
		return;
	}
	static std::string UIntToString(unsigned long long value)
	{
		std::string str;

		UIntToString(value, str);

		return str;
	}
	static void UIntToString(unsigned long long value, OUT std::string& out_str)
	{
		stdutil::format(out_str, "%LLu", value);
		return;
	}

public:
	static std::wstring FloatToWide(float value)
	{
		std::wstring str;

		FloatToWide(value, str);

		return str;
	}
	static void FloatToWide(float value, OUT std::wstring& out_str)
	{
		stdutil::format(out_str, L"%.3f", value);
		return;
	}
	static std::string FloatToString(float value)
	{
		std::string str;
		stdutil::format(str, "%.3f", value);
		return str;
	}
	static void FloatToString(float value, OUT std::string& out_str)
	{
		stdutil::format(out_str, "%.3f", value);
		return;
	}

public:
	static bool ToLower(std::string& str)
	{
		std::transform(str.begin(), str.end(), str.begin(), tolower);
		return true;
	}
	static bool ToLower(std::wstring& str)
	{
		std::transform(str.begin(), str.end(), str.begin(), tolower);
		return true;
	}

public:
	static bool ToUpper(std::string& str)
	{
		std::transform(str.begin(), str.end(), str.begin(), toupper);
		return true;
	}
	static bool ToUpper(std::wstring& str)
	{
		std::transform(str.begin(), str.end(), str.begin(), toupper);
		return true;
	}

public:
	static bool IsNumber(const char* str)
	{
		if (!str) return false;
		size_t len = strlen(str);
		if (!len) return false;

		for (size_t i = 0; i < len; ++i)
			if (str[i] != '.' && str[i] != '-' && !::isdigit(static_cast<unsigned char>(str[i]))) return false;

		return true;
	}
	static bool IsNumber(const wchar_t* wstr)
	{
		if (!wstr) return false;

		size_t len = wcslen(wstr);
		if (!len) return false;

		for (size_t i = 0; i < len; ++i)
			if (wstr[i] != '.' && !::iswdigit(wstr[i])) return false;
		return true;
	}

public:
	static void Trim(std::string& str)
	{
		size_t npos = str.find_last_not_of(" \t\v\n") + 1;

		if (npos != std::string::npos)
			str.replace(npos, str.length() - npos, "");
	}
	static void Trim(std::wstring& str)
	{
		size_t npos = str.find_last_not_of(L" \t\v\n") + 1;
		if (npos != std::wstring::npos)
			str.replace(npos, str.length() - npos, L"");
	}
};

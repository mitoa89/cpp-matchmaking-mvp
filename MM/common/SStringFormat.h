#pragma once

/*
	%[<|><{>][flags][:padchar][width][.precision]type-char[<|><}>]

	[flags] optional flag.
	'-':  left alignment (default is right alignment)
	'=':  centered alignment
	'+':  show sign
	'!':  don't print int that is *0* or string that is *null*
	'L':  use long instead of int
	'LL': use long long instead of long
	'0':  pad with pad char (if pad char is not specified, then fill with 0's)
	'#':  show base. o- 0, x- 0x, X- 0X

	[:padchar] optional flag. Default value depends on [flags].
	[width] optional flag. Specifies a minimal width for the string resulting form the conversion.
	[.precision] optional flag. When outputting a floatting type number, it sets the maximum number of digits.
				 When used with type-char s or S the conversion string is truncated to the precision first chars.
	type-char:
	'b':            binary output
	'o':            octal output
	'i', 'u', 'd':  decimal output (signed, unsigned)
	'x':            hexadecimal output ('X' upper case)
	'f':            fixed float format
	's', 'S':       string output
	'c', 'C':       char output
	'%':            print '%'
*/

#include <string>
#include <vector>
#include <stdarg.h>

#if _MSC_VER >= 1100
#pragma warning(disable: 4996)
#else
#define NO_MBSTOWCS_S
#define NO_WCSTOMBS_S
#define NO_SPRINTF_S
#endif

namespace stdutil {

	// how many digits we want in width, precision and arg number?
	// please note, that too big number can hit at safety
	enum { eMaxSize = 4 }; //enum _ENUM
	enum { eMaxIntTSize = 128 }; //enum _ENUM

	enum EFormatFlags
	{
		ffLeftAlign = 1,
		ffRightAlign = 2,    // (default)
		ffCenterAlign = (ffLeftAlign | ffRightAlign),

		ffShowSign = 4,
		ffShowBase = 8,
		ffShowPadChar = 16,
		ffShowHexCapital = 32,
		ffUseLong = 64,
		ffUse64 = 128,
		ffNoNULL = 256
	}; //enum EFormatFlags

	template<class charT>
	union format_arg
	{
		// int
		int                 fa_int;
		unsigned int        fa_uint;

		// long
		long                fa_long;
		unsigned long       fa_ulong;
		long long           fa_longlong;
		unsigned long long  fa_ulonglong;

		// double
		double              fa_double;
		long double         fa_longdouble;

		// char
		charT               fa_charT;
		char                fa_char;
		wchar_t             fa_wchar;

		// pointers
		const charT* fa_cpcharT;
		const char* fa_cpchar;
		const wchar_t* fa_cpwchar;
		void* fa_pointer;
	}; //union format_arg

	// This structure holds everything that is written in format string after % and till type-char
	template<class charT>
	struct format_info
	{
		int         iFlags;         // default: ffRightAlign
		int         iBase;          // default: 10
		charT       ctPadChar;      // default: space (for string), 0 (for numbers)
		int         iWidth;         // default: 0
		int         iPrecision;     // default: 0
		charT       ctTypeChar;
	}; //struct format_info

	template<class charT>
	inline bool is_digit(const charT cct)
	{
		return ('0' <= cct && '9' >= cct);
	}

	template<class charT>
	inline bool is_typechar(const charT cct)
	{
		return ('b' == cct ||   // binary (unsigned int/long)
			'o' == cct ||   // octal (unsigned int/long)
			'i' == cct ||   // decimal (int/long)
			'd' == cct ||   // decimal (long)
			'u' == cct ||   // decimal (unsigned int/long)
			'x' == cct ||   // hex (unsigned int/long)
			'X' == cct ||   // HEX (unsigned int/long)
			'f' == cct ||   // double/long double
			's' == cct ||   // stringT
			'S' == cct ||   // wstring
			'c' == cct ||   // charT
			'C' == cct);    // wchar_t
	}

	template<class charT>
	size_t strlen_t(const charT* cpct)
	{
		size_t iLn = 0;
		while (*cpct++) iLn++;
		return iLn;
	}

	template<class charT>
	const charT* read_flags(const charT* cpct, format_info<charT>* pfi)
	{
		pfi->iFlags = ffRightAlign;
		pfi->iBase = 10;
		pfi->ctPadChar = 0x20;
		pfi->iWidth = 0;
		pfi->iPrecision = 0;

		while (*cpct)
		{
			switch (*cpct)
			{
			case '+':
				pfi->iFlags |= ffShowSign;
				cpct++;   // skip '+'
				break;

			case '-':
				pfi->iFlags &= ~(ffRightAlign | ffLeftAlign);
				pfi->iFlags |= ffLeftAlign;
				cpct++;   // skip '-'
				break;

			case '=':
				pfi->iFlags |= (ffRightAlign | ffLeftAlign);
				cpct++;   // skip '='
				break;

			case '0':
				pfi->ctPadChar = '0';
				cpct++;   // skip '0'
				break;

			case '#':
				pfi->iFlags |= ffShowBase;
				cpct++;   // skip '#'
				break;

			case '!':
				pfi->iFlags |= ffNoNULL;
				cpct++;   // skip '!'
				break;

			case 'L':
				if (ffUseLong == (pfi->iFlags & ffUseLong)) pfi->iFlags |= ffUse64;
				else pfi->iFlags |= ffUseLong;
				cpct++;   // skip 'L'
				break;

				// no proper flag, just return
			default: return cpct;
			} //siwtch
		} //while

		// oops, end of string
		return cpct;
	}

	template<class charT>
	void put_char(std::basic_string<charT, std::char_traits<charT>, std::allocator<charT> >& stOut,
		const charT* cpct,
		format_info<charT>* pfi)
	{
		static const charT cctNULL[] = { '(', 'n', 'u', 'l', 'l', ')' };
		if (0 == cpct)
		{
			// do not nothing is user said so :)
			if (ffNoNULL == (pfi->iFlags & ffNoNULL)) return;
			cpct = cctNULL;
		} //if

		size_t iLn = strlen_t<charT>(cpct);

		// if precision is less than actual string length, then we put less of string
		//버그 코드로 보임 - 추후 확실하게 테스트해볼것!!!
		//if (pfi->iPrecision && pfi->iPrecision < static_cast<int> (iLn)) iLn = pfi->iPrecision;

		size_t iWidth = iLn; // this is actual number of chars to be inserted
		if (pfi->iWidth > static_cast<int> (iWidth)) iWidth = pfi->iWidth;

		if ((ffShowSign == (pfi->iFlags & ffShowSign)) &&
			('-' == *cpct || '+' == *cpct))
		{
			stOut += *cpct++; // skip '+' or '-'
			iLn--;
			iWidth--;
		} //if

		if (ffShowBase == (pfi->iFlags & ffShowBase))
		{
			if (8 == pfi->iBase)
			{
				stOut += '0';
				iWidth--;
			} //if
			else if (16 == pfi->iBase)
			{
				iWidth -= 2;
				stOut += '0';
				if (ffShowHexCapital == (pfi->iFlags & ffShowHexCapital)) stOut += 'X';
				else stOut += 'x';
			} //else if
		} //if

		size_t iPos = stOut.length(); // left align

		if (iWidth > iLn)
		{
			// fill entire buffer with pad chars
			stOut.append(iWidth, pfi->ctPadChar);

			if (ffCenterAlign == (pfi->iFlags & ffCenterAlign)) iPos += (iWidth - iLn) / 2;
			else if (ffRightAlign == (pfi->iFlags & ffRightAlign)) iPos += iWidth - iLn;
		} //if

		stOut.replace(iPos, iLn, cpct, iLn);
	}

	// 4 special cases
	template<class charT>
	inline void convert_put(std::basic_string<charT, std::char_traits<charT>, std::allocator<charT> >& stOut,
		const char* cpc,
		format_info<charT>* pfi,
		char /* useless */)
	{
		put_char<charT>(stOut, cpc, pfi);
	}
	template<class charT>
	inline void convert_put(std::basic_string<charT, std::char_traits<charT>, std::allocator<charT> >& stOut,
		const wchar_t* cpwc,
		format_info<charT>* pfi,
		wchar_t /* useless */)
	{
		put_char<charT>(stOut, cpwc, pfi);
	}
	// convert char-to-wchar
	template<class charT>
	void convert_put(std::basic_string<charT, std::char_traits<charT>, std::allocator<charT> >& stOut,
		const char* cpc,
		format_info<charT>* pfi,
		wchar_t /* useless */)
	{
		size_t iStrLn = strlen_t<char>(cpc);
		size_t iLn = 0;
#ifdef NO_MBSTOWCS_S
		iLn = mbstowcs(0, cpc, iStrLn);
#else
		errno_t err = mbstowcs_s(&iLn, 0, 0, cpc, iStrLn);
#endif //NO_MBSTOWCS_S

		//버그 코드로 보임 - 추후 확실하게 테스트해볼것!!!
		//if (0 <= iLn) put_char<charT> (stOut, 0, pfi);
		//else
		{
			wchar_t* pwc = new wchar_t[iLn + 1];
#ifdef NO_MBSTOWCS_S
			iLn = mbstowcs(pwc, cpc, iStrLn);
			if (iLn <= 0)
			{
				delete[] pwc;
				return;
			} //if
			*(pwc + iLn) = '\0';
#else
			err = mbstowcs_s(&iLn, pwc, iLn + 1, cpc, iStrLn);
#endif //NO_MBSTOWCS_S
			put_char<charT>(stOut, pwc, pfi);
			delete[] pwc;
		} //else
	}
	// convert wchar-to-char
	template<class charT>
	void convert_put(std::basic_string<charT, std::char_traits<charT>, std::allocator<charT> >& stOut,
		const wchar_t* cpwc,
		format_info<charT>* pfi,
		char /* useless */)
	{
		size_t iStrLn = strlen_t<wchar_t>(cpwc);
		size_t iLn = 0;
#ifdef NO_WCSTOMBS_S
		iLn = wcstombs(0, cpwc, iStrLn);
#else
		errno_t err = wcstombs_s(&iLn, 0, 0, cpwc, iStrLn);
#endif //NO_WCSTOMBS_S

		//버그 코드로 보임 - 추후 확실하게 테스트해볼것!!!
		//if (0 == iLn) put_char<charT> (stOut, 0, pfi);
		//else
		{
			char* pc = new char[iLn + 1];
#ifdef NO_WCSTOMBS_S
			iLn = wcstombs(pc, cpwc, iStrLn);
			if (iLn <= 0)
			{
				delete[] pc;
				return;
			} //if
			*(pc + iLn) = '\0';
#else
			err = wcstombs_s(&iLn, pc, iLn + 1, cpwc, iStrLn);
#endif //NO_WCSTOMBS_S
			put_char<charT>(stOut, pc, pfi);
			delete[] pc;
		} //else
	}

	template<class charT, class intT>
	void put_int(std::basic_string<charT, std::char_traits<charT>, std::allocator<charT> >& stOut,
		intT it,
		format_info<charT>* pfi)
	{
		// create array of charT, but don't fill it with 0's
		charT ctData[eMaxIntTSize];
		charT* pct = &ctData[eMaxIntTSize - 1];

		*pct = 0;

		if (0 == it)
		{
			// do not nothing is user said so :)
			if (ffNoNULL == (pfi->iFlags & ffNoNULL)) return;
			*(--pct) = '0';
			if (ffShowSign == (pfi->iFlags & ffShowSign)) *(--pct) = '+';
		} //if
		else
		{
			int i;
			bool bNeg = (it < 0);

			int iHexChar = -10; // 'a' (97) - 10, because: if (i >= 10)...
			if (ffShowHexCapital == (pfi->iFlags & ffShowHexCapital)) iHexChar += 'A';
			else iHexChar += 'a';

			while (it)
			{
				i = static_cast<int> (it % pfi->iBase);
				if (bNeg) i = -i;

				if (i >= 10) *(--pct) = static_cast<charT>(i + iHexChar);
				else *(--pct) = static_cast<charT>(i + '0');

				it /= pfi->iBase;
			} //while

			if (bNeg) *(--pct) = '-';
			else if (ffShowSign == (pfi->iFlags & ffShowSign)) *(--pct) = '+';
		} //else

		put_char<charT>(stOut, pct, pfi);
	}

	template<class charT>
	size_t vformat(std::basic_string<charT, std::char_traits<charT>, std::allocator<charT> >& stOut,
		const charT* cpctFormat,
		va_list vargs)
	{
		// BAD STYLE - CODE REDUSE :)
#define UNSIGNED_PRINT                                                                          \
    if (ffUseLong != (fi.iFlags & ffUseLong))                                                   \
        put_int<charT, unsigned int> (stOut, va_arg (vargs, unsigned int), &fi);                \
    else if (ffUse64 == (fi.iFlags & ffUse64))                                                  \
        put_int<charT, unsigned long long> (stOut, va_arg (vargs, unsigned long long), &fi);    \
    else                                                                                        \
        put_int<charT, unsigned long> (stOut, va_arg (vargs, unsigned long), &fi);

	// real code begins here
	// ------- ------- ------- ------- ------- ------- -------
	//stOut = "";
		stOut.clear(); // somehow slower?

		const charT* cpct = cpctFormat;

		while (*cpct)
		{
			if ('%' == *cpct)
			{
				cpct++;  // skip '%'

				// end of string, this shouldn't happen on normal format
				if ('\0' == *cpct) return (cpct - cpctFormat);

				// skip this section, this is not format
				if ('%' == *cpct)
				{
					stOut += *cpct++;  // skip '%'
					continue;
				} //if

				int iCover = 0; // 0: none, 1: '{', 2: '|'
				if ('{' == *cpct || '|' == *cpct)
				{
					if ('{' == *cpct) iCover = 1;
					else iCover = 2;
					cpct++; // skip '{' or '|'
				} //if

				format_info<charT> fi;

				// set default format info and read flags if there are any
				cpct = read_flags(cpct, &fi);
				if ('\0' == *cpct) return (cpct - cpctFormat);

				// is it a pad char?
				if (':' == *cpct)
				{
					cpct++; // skip ':'
					if ('\0' == *cpct) return (cpct - cpctFormat);
					fi.ctPadChar = *cpct++; // read and skip pad char
					if ('\0' == *cpct) return (cpct - cpctFormat);
				} //if

				// is it width?
				if ('*' == *cpct)
				{
					cpct++; // skip '*'
					fi.iWidth = va_arg(vargs, int);
				} //if
				else if (is_digit(*cpct))
				{
					char cInt[eMaxSize] = { 0 };
					for (int i = 0; i < eMaxSize && is_digit(*cpct); i++, cpct++)
						cInt[i] = static_cast<char> (*cpct);

					// it is bigger number than expected or it is end of string
					if ('\0' == *cpct || is_digit(*cpct)) return (cpct - cpctFormat);
					fi.iWidth = atoi(cInt);
				} //else if

				// last, but not least moment: the precision
				if ('.' == *cpct)
				{
					cpct++; // skip '.'
					// end of string?
					if ('\0' == *cpct) return (cpct - cpctFormat);

					if ('*' == *cpct)
					{
						cpct++; // skip '*'
						fi.iPrecision = va_arg(vargs, int);
					} //if
					else
					{
						char cInt[eMaxSize] = { 0 };
						for (int i = 0; i < eMaxSize && is_digit(*cpct); i++, cpct++)
							cInt[i] = static_cast<char> (*cpct);

						// it is bigger number than expected or it is end of string
						if ('\0' == *cpct || is_digit(*cpct)) return (cpct - cpctFormat);
						fi.iPrecision = atoi(cInt);
					} //else
				} //if

				// at last, check for type-char
				if (!is_typechar(*cpct)) return (cpct - cpctFormat);
				fi.ctTypeChar = *cpct++;

				// remove user's jokes R.C. :)
				// add additional stuff to format_info
				switch (fi.ctTypeChar)
				{
				case 'b':
					fi.iBase = 2;
					fi.iFlags &= ~(ffShowSign | ffShowBase);
					UNSIGNED_PRINT;
					break;

				case 'o':
					fi.iBase = 8;
					fi.iFlags &= ~ffShowSign;
					UNSIGNED_PRINT;
					break;

				case 'x':
					fi.iBase = 16;
					fi.iFlags &= ~ffShowSign;
					UNSIGNED_PRINT;
					break;

				case 'X':
					fi.iBase = 16;
					fi.iFlags &= ~ffShowSign;
					fi.iFlags |= ffShowHexCapital;
					UNSIGNED_PRINT;
					break;

				case 'u': UNSIGNED_PRINT; break;

				case 'i':
				case 'd':
				{
					if (ffUseLong != (fi.iFlags & ffUseLong))
						put_int<charT, int>(stOut, va_arg(vargs, int), &fi);
					else if (ffUse64 == (fi.iFlags & ffUse64))
						put_int<charT, long long>(stOut, va_arg(vargs, long long), &fi);
					else
						put_int<charT, long>(stOut, va_arg(vargs, long), &fi);

					break;
				} //case

				case 's':
					fi.iFlags &= ~(ffShowSign | ffShowBase | ffUse64 | ffUseLong);
					put_char(stOut, va_arg(vargs, const charT*), &fi);
					break;

				case 'S':
					fi.iFlags &= ~(ffShowSign | ffShowBase | ffUse64 | ffUseLong);
					convert_put<charT>(stOut, va_arg(vargs, const wchar_t*), &fi, fi.ctPadChar);
					break;

				case 'c':
				{
					fi.iFlags &= ~(ffShowSign | ffShowBase | ffUse64 | ffUseLong);

					union format_arg<charT> farg;
					farg.fa_cpcharT = va_arg(vargs, const charT*);

					charT ct[2] = { farg.fa_charT, 0 };
					put_char(stOut, ct, &fi);
					break;
				} //case

				case 'C':
				{
					fi.iFlags &= ~(ffShowSign | ffShowBase | ffUse64 | ffUseLong);

					union format_arg<charT> farg;
					farg.fa_cpwchar = va_arg(vargs, const wchar_t*);

					wchar_t ct[2] = { farg.fa_wchar, 0 };
					convert_put<charT>(stOut, ct, &fi, fi.ctPadChar);
					break;
				} //case

				case 'f':
				{
					fi.iFlags &= ~(ffShowBase | ffUse64);
					char cFloat[64];

					if (ffUseLong == (fi.iFlags & ffUseLong))
					{
						if (fi.iPrecision && fi.iPrecision < 64)
#if !defined(NO_SPRINTF_S)
							sprintf_s<64>(cFloat, "%.*Lf", fi.iPrecision, va_arg(vargs, long double));
#else
							snprintf(cFloat, sizeof(cFloat), "%.*Lf", fi.iPrecision, va_arg(vargs, long double));
#endif // !NO_PRINTF_S
						else
#if !defined(NO_SPRINTF_S)
							sprintf_s<64>(cFloat, "%Lf", va_arg(vargs, long double));
#else
							snprintf(cFloat, sizeof(cFloat), "%Lf", va_arg(vargs, long double));
#endif // !NO_SPRINTF_S
					} //if
					else
					{
						if (fi.iPrecision && fi.iPrecision < 64)
#if !defined(NO_SPRINTF_S)
							sprintf_s<64>(cFloat, "%.*f", fi.iPrecision, va_arg(vargs, double));
#else
							snprintf(cFloat, sizeof(cFloat), "%.*f", fi.iPrecision, va_arg(vargs, double));
#endif // !NO_SPRINTFF_S
						else
#if !defined(NO_SPRINTF_S)
							sprintf_s<64>(cFloat, "%f", va_arg(vargs, double));
#else
							snprintf(cFloat, sizeof(cFloat), "%f", va_arg(vargs, double));
#endif // !NO_SPRINTF_S
					} //else

					convert_put<charT>(stOut, cFloat, &fi, fi.ctPadChar);
					break;
				} //case
				} //switch

				if (iCover)
				{
					if (1 == iCover && '}' != *cpct) return (cpct - cpctFormat);
					if (2 == iCover && '|' != *cpct) return (cpct - cpctFormat);

					cpct++; // skip '}' or '|'
				} //if
			} //if
			else stOut += *cpct++;
		} //while

		return 0;
	}

	inline size_t format(std::string& sOut, const char* cpcFormat, ...)
	{
		va_list vargs;
		va_start(vargs, cpcFormat);
		size_t iRes = vformat<char>(sOut, cpcFormat, vargs);
		va_end(vargs);
		return iRes;
	}

	inline size_t format(std::wstring& sOut, const wchar_t* cpcFormat, ...)
	{
		va_list vargs;
		va_start(vargs, cpcFormat);
		size_t iRes = vformat<wchar_t>(sOut, cpcFormat, vargs);
		va_end(vargs);
		return iRes;
	}

} //namespace stdutil

// FindCrypt - find constants used in crypto algorithms
// Copyright 2006 Ilfak Guilfanov <ig@hexblog.com>
// Copyright 2011 Vlad Tsyrklevich <vlad@tsyrklevich.net>
// This is a freeware program.
// This copytight message must be kept intact.

// This plugin looks for constant arrays used in popular crypto algorithms.
// If a crypto algorithm is found, it will rename the appropriate locations
// of the program and put bookmarks on them.

// Version 2-with-mmx
// Adapted to x64dbg
#include <set>
#include <thread>
#include "findcrypt.h"

Findcrypt::Findcrypt(duint VirtualStart, duint VirtualEnd)
{
	// Sanity check: make sure there are no duplicate entries anywhere
	static bool initOnce = false;

	if (!initOnce)
	{
		initOnce = true;
		VerifyConstants(non_sparse_consts);
		VerifyConstants(sparse_consts);
	}

	// Real class constructor code
	m_StartAddress	= VirtualStart;
	m_EndAddress	= VirtualEnd;
	m_DataSize		= VirtualEnd - VirtualStart;
	m_Data			= (PBYTE)malloc(m_DataSize);

	m_AESNICount	= 0;
	m_CryptoCount	= 0;

	// Read the remote memory into the local buffer
	if (!DbgMemRead(VirtualStart, m_Data, m_DataSize))
		memset(m_Data, 0, m_DataSize);
}

Findcrypt::~Findcrypt()
{
	if (m_Data)
		free(m_Data);
}

void Findcrypt::VerifyConstants(const array_info_t *consts)
{
	std::set<std::string> myset;

	// Verifies that all algorithm entries are different
	for (const array_info_t *ptr = consts; ptr->size != 0; ptr++)
	{
		std::string s((const char*)ptr->array, ptr->size);

		if (!myset.insert(s).second)
		{
			dprintf("duplicate array %s!", ptr->name);
			__debugbreak();
		}
	}
}

void Findcrypt::ScanConstants()
{
	for (duint ea = m_StartAddress; ea < m_EndAddress; ea = ea + 1)
	{
		// Update the status bar every 65k bytes
		if ((ea % 0x10000) == 0)
			ShowAddress(ea);

		// Check against normal constants
		BYTE b = GetByte(ea);

		for (const array_info_t *ptr = non_sparse_consts; ptr->size != 0; ptr++)
		{
			if (b != GetFirstByte(ptr))
				continue;

			if (MatchArrayPattern(ea, ptr))
			{
				dprintf("%p: Found const array %s (used in %s)\n", ea, ptr->name, ptr->algorithm);
				DbgSetAutoCommentAt(ea, ptr->algorithm);
				DbgSetAutoLabelAt(ea, ptr->name);
				m_CryptoCount++;
				break;
			}
		}

		// Check against sparse constants
		for (const array_info_t *ptr = sparse_consts; ptr->size != 0; ptr++)
		{
			if (b != GetFirstByte(ptr))
				continue;

			if (MatchSparsePattern(ea, ptr))
			{
				dprintf("%p: Found sparse constants for %s\n", ea, ptr->algorithm);
				DbgSetAutoCommentAt(ea, ptr->algorithm);
				m_CryptoCount++;
				break;
			}
		}
	}

	for (duint ea = m_StartAddress; ea < m_EndAddress; ea = ea + 1)
	{
		// Update the status bar every 65k bytes
		if ((ea % 0x10000) == 0)
			ShowAddress(ea);

		if (GetByte(ea) == 0x66 && GetByte(ea + 1) == 0x0f)
		{
			char *instruction = nullptr;

			if (GetByte(ea + 2) == 0x38)
			{
				switch (GetByte(ea + 3))
				{
				case 0xdb: instruction = "AESIMC";		break;
				case 0xdc: instruction = "AESENC";		break;
				case 0xdd: instruction = "AESENCLAST";	break;
				case 0xde: instruction = "AESDEC";		break;
				case 0xdf: instruction = "AESDECLAST";	break;
				}
			}
			else if (GetByte(ea + 2) == 0x3a && GetByte(ea + 3) == 0xdf)
				instruction = "AESKEYGENASSIST";

			if (instruction)
			{
				dprintf("%p: May be %s\n", ea, instruction);
				m_AESNICount++;
			}
		}
	}
}

BYTE Findcrypt::GetFirstByte(const array_info_t *ai)
{
	const BYTE *ptr = (const BYTE *)ai->array;

#ifndef IS_LITTLE_ENDIAN
	if (!inf.mf)
		return ptr[0];
#endif // IS_LITTLE_ENDIAN

	return ptr[ai->elsize - 1];
}

bool Findcrypt::MatchArrayPattern(duint Address, const array_info_t *ai)
{
	BYTE *ptr = (BYTE *)ai->array;

	for (size_t i = 0; i < ai->size; i++)
	{
		switch (ai->elsize)
		{
		case 1:
			if (GetByte(Address) != *(BYTE *)ptr)
				return false;
			break;
		case 2:
			if (GetWord(Address) != *(WORD *)ptr)
				return false;
			break;
		case 4:
			if (GetLong(Address) != *(ULONG *)ptr)
				return false;
			break;
		case 8:
			if (GetQword(Address) != *(UINT64 *)ptr)
				return false;
			break;
		default:
			dprintf("interr: unexpected array '%s' element size %d\n",
				ai->name, ai->elsize);
			__debugbreak();
		}

		ptr		+= ai->elsize;
		Address += ai->elsize;
	}

	return true;
}

bool Findcrypt::MatchSparsePattern(duint Address, const array_info_t *ai)
{
	const unsigned int *ptr = (const unsigned int *)ai->array;

	// Match first 4 bytes
	if (GetLong(Address) != *ptr++)
		return false;

	Address += 4;

	// Continue with looping the remaining pattern
	for (size_t i = 1; i < ai->size; i++)
	{
		unsigned int c = *ptr++;

#ifndef IS_LITTLE_ENDIAN
		if (inf.mf)
			c = swap32(c);
#endif // IS_LITTLE_ENDIAN

		// Look for the constant in the next N bytes
		const size_t N = 64;
		BYTE mem[N + 4];
		GetManyBytes(Address, mem, sizeof(mem));
		int j;
		for (j = 0; j < N; j++)
		{
			if (*(ULONG*)(mem + j) == c)
				break;
		}

		if (j == N)
			return false;

		Address += j + 4;
	}

	return true;
}

void Findcrypt::ShowAddress(duint Address)
{
	char buf[64];

	sprintf_s(buf, "\nAddress: %p\n", Address);
	GuiAddStatusBarMessage(buf);
}

void FindcryptScanRange(duint Start, duint End)
{
	dprintf("Starting a crypto scan of range %p to %p...\n", Start, End);

	// Run on this thread (which should be a command thread)
	Findcrypt scanner(Start, End);
	scanner.ScanConstants();

	dprintf("Found %d possible AES-NI instructions and %d constant arrays.\n", scanner.AESNICount(), scanner.CryptoCount());
}

void FindcryptScanModule()
{
	duint moduleStart = DbgGetCurrentModule();
	duint moduleEnd = moduleStart + DbgFunctions()->ModSizeFromAddr(moduleStart);

	FindcryptScanRange(moduleStart, moduleEnd);
}

void FindcryptScanAll()
{
	int totalAES	= 0;
	int totalCrypto	= 0;

	dprintf("Starting a crypto scan for all memory ranges...\n");

	DbgEnumMemoryRanges([&](duint Start, duint End)
	{
		Findcrypt scanner(Start, End);
		scanner.ScanConstants();

		// Increment counters
		totalAES	+= scanner.AESNICount();
		totalCrypto += scanner.CryptoCount();

		return true;
	});

	dprintf("Found %d possible AES-NI instructions and %d constant arrays.\n", totalAES, totalCrypto);
}

void Plugin_FindcryptLogo()
{
	dprintf("---- Findcrypt v2 with AES-NI extensions ----\n");
	dprintf("Executing self test...\n");

	Findcrypt test(0, 1);
	test.VerifyConstants(non_sparse_consts);
	test.VerifyConstants(sparse_consts);

	//
	// Displays the startup information for this build of findcrypt
	//
	int counter = 1;

	auto DisplayArray = [&](const array_info_t *ai)
	{
		for (const char *prev = nullptr; ai->size != 0; ai++)
		{
			if (!prev || (prev && _stricmp(prev, ai->algorithm) != 0))
			{
				dprintf("%s, ", ai->algorithm);

				if ((++counter) % 10 == 0)
					dprintf("\n\t");
			}

			prev = ai->algorithm;
		}
	};

	dprintf("Available constant checking:\n\t");
	DisplayArray(non_sparse_consts);
	DisplayArray(sparse_consts);
	dprintf("\n");
}
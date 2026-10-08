#include "HashCommon.h"
#include <shlobj.h>

namespace
{
void PrintUsage()
{
    std::wcerr
        << L"usage:\n"
        << L"  UserChoiceLatestHash.exe -set <assoc> <progid> [<assoc2> <progid2> ...]\n"
        << L"  UserChoiceLatestHash.exe -get <assoc>\n"
        << L"  UserChoiceLatestHash.exe -verify <assoc>\n"
        << L"  UserChoiceLatestHash.exe -debug <canonical_input>\n";
}
} // namespace

namespace UserChoiceLatestHash
{
void PrintDebugResult(const std::wstring &hash, const DebugData &dbg)
{
    std::wcout << L"hash: " << hash << L"\n";
    std::wcout << L"packed_len_bytes: " << std::hex << (dbg.packed_words.size() * 2U) << std::dec << L"\n";
    std::wcout << L"packed: ";
    for (size_t i = 0; i < dbg.packed_words.size(); ++i)
    {
        wchar_t tmp[8];
        swprintf(tmp, 8, L"%04X", dbg.packed_words[i]);
        std::wcout << tmp;
    }
    std::wcout << L"\n";

    std::wcout << L"md5: ";
    for (size_t i = 0; i < 4U; ++i)
    {
        wchar_t tmp[16];
        swprintf(tmp, 16, L"%08X", dbg.md5_words[i]);
        std::wcout << tmp;
    }
    std::wcout << L"\n";

    std::wcout << L"A0=" << std::hex << dbg.pair_a[0]
               << L" A1=" << dbg.pair_a[1]
               << L" B0=" << dbg.pair_b[0]
               << L" B1=" << dbg.pair_b[1] << std::dec << L"\n";
}

int RunStandaloneCli(int argc, wchar_t **argv)
{
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    if (wcscmp(argv[1], L"-get") == 0)
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        std::wstring progid;
        if (GetAssociation(argv[2], &progid))
        {
            std::wcout << progid << L"\n";
            return 0;
        }
        return 1;
    }

    WorkingSeeds seeds;
    ZeroMemory(&seeds, sizeof(seeds));

    if (wcscmp(argv[1], L"-set") == 0)
    {
        if (argc < 4 || ((argc - 2) % 2 != 0))
        {
            PrintUsage();
            return 1;
        }
        LoadProvidedSeeds(&seeds);
        int success_count = 0;
        int fail_count = 0;
        for (int i = 2; i < argc; i += 2)
        {
            std::wstring assoc = argv[i];
            std::wstring progid = argv[i + 1];
            if (SetAssociation(assoc, progid, seeds))
            {
                std::wcout << L"Set: " << assoc << L" -> " << progid << L"\n";
                success_count++;
            }
            else
            {
                std::wcerr << L"Failed to set: " << assoc << L"\n";
                fail_count++;
            }
        }
        SHChangeNotify(0x8000000 /* SHCNE_ASSOCCHANGED */, 0x1000 /* SHCNF_FLUSH */, NULL, NULL);
        return (fail_count > 0 && success_count == 0) ? 1 : 0;
    }

    if (wcscmp(argv[1], L"-set-file") == 0)
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        FILE *f = NULL;
        if (_wfopen_s(&f, argv[2], L"r, ccs=UTF-8") != 0 || !f)
        {
            std::wcerr << L"Failed to open file: " << argv[2] << L"\n";
            return 1;
        }
        LoadProvidedSeeds(&seeds);
        wchar_t line[1024];
        int success_count = 0;
        int fail_count = 0;
        while (fgetws(line, 1024, f))
        {
            std::wstring s = line;
            while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'\t'))
                s.pop_back();
            size_t start = 0;
            while (start < s.size() && (s[start] == L' ' || s[start] == L'\t'))
                start++;
            if (start >= s.size() || s[start] == L'#')
                continue;
            s = s.substr(start);
            size_t sep = s.find_first_of(L" \t=");
            if (sep == std::wstring::npos)
                continue;
            std::wstring assoc = s.substr(0, sep);
            size_t val_start = s.find_first_not_of(L" \t=", sep);
            if (val_start == std::wstring::npos)
                continue;
            std::wstring progid = s.substr(val_start);
            if (SetAssociation(assoc, progid, seeds))
            {
                std::wcout << L"Set: " << assoc << L" -> " << progid << L"\n";
                success_count++;
            }
            else
            {
                std::wcerr << L"Failed to set: " << assoc << L"\n";
                fail_count++;
            }
        }
        fclose(f);
        SHChangeNotify(0x8000000 /* SHCNE_ASSOCCHANGED */, 0x1000 /* SHCNF_FLUSH */, NULL, NULL);
        return (fail_count > 0 && success_count == 0) ? 1 : 0;
    }

    if (wcscmp(argv[1], L"-verify") == 0)
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        LoadProvidedSeeds(&seeds);
        AssocContext ctx;
        if (!VerifyCurrentAssociation(argv[2], seeds, &ctx))
        {
            std::wcerr << L"verification context load failed\n";
            return 1;
        }
        return PrintVerificationResult(ctx);
    }

    if (wcscmp(argv[1], L"-debug") == 0)
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        LoadProvidedSeeds(&seeds);
        std::wstring hash;
        DebugData dbg;
        if (!ComputeHash(argv[2], seeds, false, &hash, &dbg))
        {
            std::wcerr << L"debug hash computation failed\n";
            return 1;
        }
        PrintDebugResult(hash, dbg);
        return 0;
    }

    PrintUsage();
    return 1;
}
} // namespace UserChoiceLatestHash


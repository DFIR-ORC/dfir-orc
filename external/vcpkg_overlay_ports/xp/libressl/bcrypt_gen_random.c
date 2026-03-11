#include <windows.h>
#include <wincrypt.h>

typedef int (__stdcall *FN_BCRYPT_GEN_RANDOM)(HANDLE hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer, ULONG dwFlags);

int BCryptGenRandomWrapper(HANDLE hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer, ULONG dwFlags);
static int BCryptGenRandom_xp(HANDLE hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer, ULONG dwFlags);

int BCryptGenRandomWrapper(HANDLE hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer, ULONG dwFlags)
{
    const char szDll[] = "bcrypt.dll";
    HMODULE hDll = GetModuleHandleA(szDll);
    if (hDll == NULL)
    {
    	hDll = LoadLibraryA(szDll);
    }

    if (hDll != NULL)
    {
        FN_BCRYPT_GEN_RANDOM fn_BCryptGenRandom = (FN_BCRYPT_GEN_RANDOM) GetProcAddress(hDll, "BCryptGenRandom");
        if (fn_BCryptGenRandom)
        {
			return fn_BCryptGenRandom(hAlgorithm, pbBuffer, cbBuffer, dwFlags);
		}
    }
	else
	{
		return BCryptGenRandom_xp(hAlgorithm, pbBuffer, cbBuffer, dwFlags);
	}
}

//
// BEWARE: I don't need entropy for processing Yara rules but as minimal xp support I use this
// basic entropy source.
//
// In case better source is needed, one could inspire on:
//
// https://github.com/openssl/openssl/blob/50eaac9f3337667259de725451f201e784599687/crypto/rand/rand_win.c#L21-L28
//
int BCryptGenRandom_xp(BCRYPT_ALG_HANDLE hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer, ULONG dwFlags)
{
	HCRYPTPROV hProvider;

	if (CryptAcquireContextW(&hProvider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT) == 0)
	{
		return -1;
	}

	if (CryptGenRandom(hProvider, cbBuffer, pbBuffer) == 0)
	{
		CryptReleaseContext(hProvider, 0);
		return -1;
	}

	CryptReleaseContext(hProvider, 0);
	return cbBuffer;
}

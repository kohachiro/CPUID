#include <stdio.h>
#include <string.h>
#include <intrin.h>


int main(int argc, char* argv[])
{
		bool IsAA64=false;
		bool Is3DNOW;
		bool Is3DNOW2;
		bool IsMMXExt;

		bool IsHTT;
		bool IsMMX;
		bool IsSSE;
		bool IsSSE2;
		bool IsSSE3;
		bool IsSSSE3;
		bool IsIA64;
		bool IsEM64T=false;
		bool IsSStep;
		int Core;
		char CPUBrandString[0x40];
		int CPUInfo[4] = {-1};

		unsigned long core;
	
			memset(CPUBrandString, 0, sizeof(CPUBrandString));

			__cpuid(CPUInfo, 0x80000002);
			memcpy(CPUBrandString, CPUInfo, sizeof(CPUInfo));

			__cpuid(CPUInfo, 0x80000003);
			memcpy(CPUBrandString + 16, CPUInfo, sizeof(CPUInfo));

			__cpuid(CPUInfo, 0x80000004);
			memcpy(CPUBrandString + 32, CPUInfo, sizeof(CPUInfo));

			__cpuid(CPUInfo, 1);

			IsHTT=(( CPUInfo[3] & 1<<28 ) != 0);
			IsMMX=(( CPUInfo[3] & 1<<23 ) != 0);
			IsSSE=(( CPUInfo[3] & 1<<25 ) != 0);
			IsSSE2=(( CPUInfo[3] & 1<<26 ) != 0);
			IsSSE3=(( CPUInfo[2] & 1<<0 ) != 0);
			IsSSSE3=(( CPUInfo[2] & 1<<9 ) != 0);
			IsIA64=(( CPUInfo[2] & 1<<30 ) != 0);
			IsSStep=(( CPUInfo[2] & 1<<7 ) != 0);
			
			__cpuid(CPUInfo, 0x80000001);
			Is3DNOW=(( CPUInfo[3] & 1<<31 ) != 0);
			Is3DNOW2=(( CPUInfo[3] & 1<<30 ) != 0);
			IsMMXExt=(( CPUInfo[3] & 1<<22 ) != 0);
			if (Is3DNOW)
			{
				IsAA64=(( CPUInfo[3] & 1<<29 ) != 0);
				__cpuid(CPUInfo, 0x80000008);
				Core=(CPUInfo[2]+1);

			}	
			else
			{
				IsEM64T=(( CPUInfo[3] & 1<<29 ) != 0);
				_asm
				{
					mov eax ,4
					mov ecx ,0
					cpuid
					mov core ,eax
				}
				Core=(((core & 0xFC000000) >> 26 )+1);
			}
	printf_s("\n%s\n", CPUBrandString);

	if (Is3DNOW)
		printf ("\n3DNow!");
	if (Is3DNOW2)
		printf ("\n3DNow!2");
	if (IsMMX)
		printf ("\nMMX");
	if (IsMMXExt)
		printf ("\nMMXExt");
	if (IsSSE)
		printf ("\nSSE");
	if (IsSSE2)
		printf ("\nSSE2");
	if (IsSSE3)
		printf ("\nSSE3");
	if (IsSSSE3)
		printf ("\nSSSE3");
	if (IsHTT)
		printf ("\nHTT");
	if (IsAA64)
		printf ("\nx86-64");
	if (IsIA64)
		printf ("\nIA64");
	if (IsEM64T)
		printf ("\nEM64T");
	if (IsSStep)
		printf ("\nSpeedStep");
	
	printf ("\nCore %d\n",Core);
	
	scanf ("%s\n","");
	return 0;
}


//
//	CPU Counting Utility for Linux, Version 1.0
//      Copyright (C) 2005 Intel Corporation.  All Rights Reserved.
//
//	cpucount.cpp -  detects multi-processor, multi-core, and Hyper-
//			Threading Technology support across IA-32 and 
//			EM64T platforms.
//
//	This application enumerates all the logical processors enabled  
//	by the OS and BIOS, and determines the topology of these enabled  
//	logical processors using information provided by the CPUID 
//	instruction.
//
//	The relevant topology can be identified using a three level 
//	decomposition of the "initial APIC ID" into the package ID, core ID, 
//	and Simultaneous Multi-Threading (SMT) ID.  Such decomposition  
//	provides a three-level map of the topology of hardware resources and
//	allows multi-threaded software to manage shared hardware resources
//      in the platform to reduce resource contention.
//
//	The multi-core detection algorithm for processor and cache topology 
//	requires all leaf functions of CPUID instructions be available. System 
//	administrators must ensure BIOS settings are not configured to restrict
//	CPUID functionalities.
//
//
//	author: Gail Lyons
//
//------------------------------------------------------------------------------

#include <sched.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <iostream>

void printHeader();
void printAttributes( int, int );
void printCapabilities( int, int, int, int, int );
void printAffinity( char * );


// EAX[31:26] - Bit 26 thru 31 contains the cores per processor pack - 1
#define CORES_PER_PROCPAK	0xFC000000

// EBX[23:16] indicates number of logical processors per package
#define NUM_LOGICAL_BITS   0x00FF0000 

// EBX[31:24] Bits 24-31 contains the 8-bit initial APIC ID for the
// processor this code is running on.  Default value = 0xff if HT
// is not supported.
#define INITIAL_APIC_ID_BITS  0xFF000000  

// EDX[28] - Bit 28 set indicates Hyper-Threading Technology is supported
// in hardware.
#define MT_BIT 0x10000000 


// Status Flag indicates hyper-threading technology and multi-core support level
#define USER_CONFIG_ISSUE		        0
#define SINGLE_CORE_AND_HT_NOT_CAPABLE	1
#define SINGLE_CORE_AND_HT_ENABLED	2
#define SINGLE_CORE_AND_HT_DISABLED	3
#define MULTI_CORE_AND_HT_NOT_CAPABLE	4
#define MULTI_CORE_AND_HT_ENABLED		5
#define MULTI_CORE_AND_HT_DISABLED	6


// This macro returns the cpu id data. 
//
#define cpuid( in, a, b, c, d )	\
    asm ( "cpuid" :					\
	  "=a" (a), "=b" (b), "=c" (c), "=d" (d) : "a" (in));


// This macro calls cpuid with 2 inputs, eax and ecx.
//
#define cpuid2( in1, in2, a, b, c, d )		\
    asm ( "cpuid" :				\
	  "=a" (a), "=b" (b), "=c" (c), "=d" (d) : 	\
	  "a" (in1), "c" (in2) );


// Return the Advanced Programmable Interface Controller (APIC) ID.
//
unsigned int get_apic_id()
{
    unsigned int a,b,c,d;
    
    cpuid( 1, a, b, c, d );
    
    return ( b & INITIAL_APIC_ID_BITS ) >> 24;
}


// Returns the maximum input value for the cpuid instruction
// supported by the chip.  Zero is returned if the cpuid 
// instruction is not supported in the hardware.
//
int get_max_input_value()
{
    unsigned int a,b,c,d;
    
    try {

	cpuid( 0, a, b, c, d );
    }
    
    catch (...) {
	return 0;
    }
    
    return  a;
}


//
// Determine the width of the bit field that can represent the value countItem.
//
unsigned int find_maskwidth(unsigned int countItem)
{
    unsigned int maskWidth;
    unsigned int count = countItem;

    asm ( 
#ifdef __x86_64__
	  "push  %%rcx\n\t"
	  "push  %%rax\n\t"
#else
	  "pushl %%ecx\n\t"
	  "pushl %%eax\n\t"
#endif
	  "xorl %%ecx, %%ecx"
	  : "=c" (maskWidth)
	  : "a" (count)
	);

    asm ( "decl %%eax\n\t"
	  "bsrw %%ax,%%cx\n\t"
	  "jz next\n\t"
	  "incw %%cx\n\t"
	  : "=c" (maskWidth)
	);

    asm
	( "next:\n\t"
#ifdef __x86_64__
	  "pop  %rax\n\t"
	  "pop  %rcx"
#else
	  "popl %eax\n\t"
	  "popl %ecx"
#endif
	);
    
    return maskWidth;
}


// Extract the subset of bit field from the 8-bit value FullID.  
// It returns the 8-bit sub ID value.
//
unsigned int getSubID(unsigned int  fullID,
		      unsigned int  maxSubIDValue,
		      unsigned int  shiftCount)
{
    unsigned int maskWidth;
    unsigned int maskBits;
    
    maskWidth = find_maskwidth( maxSubIDValue );

    maskBits  = (0xff << shiftCount) ^ (0xff << (shiftCount + maskWidth));
    
    return (fullID & maskBits);
}


// Get the affinity mask for this process.  Some Linux distros have
// the length of the mask as the second argument, and others do not.
// Check sched.h on your system.
//
int misc_sched_getaffinity( pid_t pid, cpu_set_t * affinity_mask )
{
    int stat = -1;

#if __GNUC__==3 && __GNUC_MINOR__==2 
    stat = sched_getaffinity( pid, affinity_mask );

#else
    stat = sched_getaffinity( pid, sizeof(*affinity_mask), 
				     affinity_mask );
#endif

    return stat;
}


// Install the affinity mask for the process.  Some Linux distros have 
// the length of the mask as the second argument of sched_setaffinity().
//
int misc_sched_setaffinity( pid_t pid, cpu_set_t * affinity_mask )
{
    int stat = -1;

#if __GNUC__==3 && __GNUC_MINOR__==2 
    stat = sched_setaffinity( pid, affinity_mask );

#else
    stat = sched_setaffinity( pid, sizeof(*affinity_mask),
				    affinity_mask );
#endif

    return stat;
}


// Returns a non-zero value if this system is running Genuine Intel
// hardware.
//
unsigned int GenuineIntel(void)
{
    unsigned int a,b,c,d;
    unsigned int venB = ('u' << 24) | ('n' << 16) | ('e' << 8) | 'G';
    unsigned int venD = ('I' << 24) | ('e' << 16) | ('n' << 8) | 'i';
    unsigned int venC = ('l' << 24) | ('e' << 16) | ('t' << 8) | 'n';
    
    cpuid( 0, a, b, c, d );
    
    return ( ( b == venB ) &&
	     ( d == venD ) &&
	     ( c == venC ) );
}



// Returns non-zero if Hyper-Threading Technology is supported on
// the processors and zero if not. This does not mean that
// Hyper-Threading Technology is necessarily enabled.
//
int MTSupported(void)
{
    unsigned int a,b,c,d;
    
    if ( GenuineIntel() ) {

	// older hardware does not support higher cpuid values
	int max = get_max_input_value();

	if ( max >= 1 ) {
	    // Get the chip information.
	    cpuid( 1, a, b, c, d );	
	    
	    //Indicate if HT is supported by the hardware
	    return ( d & MT_BIT );
	}
    }
    return 0;
}


// Returns the number of logical processors per physical processors.
//
int logicalProcessorsPerPackage(void)
{
    unsigned int a, b, c, d;
        
    if ( ! MTSupported() ) return 1;

    cpuid( 1, a, b, c, d );
    
    return ( b & NUM_LOGICAL_BITS ) >> 16;
}


// Returns the number of cores present per processor package.
//
int multiCoresPerProcPak( void )
{
    unsigned int a,b,c,d;
    int max, num;

    max = get_max_input_value();

    if ( max >= 4 ) {

	cpuid2 (4, 0, a, b, c, d );
   
	num = (( a & CORES_PER_PROCPAK ) >> 26 ) + 1;
    }  
    else
	num = 1;

    return num;
}


// Count the total number of available cores in the system.
//
int countAvailableCores( unsigned int tblPkgID[],
			 unsigned int tblCoreID[],
			 unsigned int numLPEnabled )
{
    
    unsigned int CoreIDBucket[256];
    int i, procNum;
    int coreIDFound;
    int total = 1;

    CoreIDBucket[0] = tblPkgID[0] | tblCoreID[0];
    
    for (procNum = 1; procNum < numLPEnabled; procNum++)
    {
	coreIDFound=0;

	for (i = 0; i < total; i++)
	{
	    // Comparing bit-fields of logical processors residing in 
	    // different packages.  Assuming the bit-masks are the same 
	    // on all processors in the system.
	    //
	    if ((tblPkgID[procNum] | tblCoreID[procNum]) == CoreIDBucket[i])
	    {
		coreIDFound = 1;
		break;
	    }	    
	}
	
	// Did not match any bucket. Create a new one.
	//
	if (! coreIDFound )   
	{
	    CoreIDBucket[i] = tblPkgID[procNum] | tblCoreID[procNum];

	    // Number of available cores in the system
	    //
	    total++;	
	    
	}
    }
    return total;
}


// Count the physical processors in the system
//
int countPhysicalPacks( unsigned int tblPkgID[], 
			unsigned int numLPEnabled )
{
    unsigned int packageIDBucket[256];
    int i, procNum;
    int packageIDFound;
    int total = 1;

    packageIDBucket[0] = tblPkgID[0];
    
    for (procNum = 1; procNum < numLPEnabled; procNum++)
    {
	packageIDFound = 0;

	for (i = 0; i < total; i++)
	{
	    // Comparing bit-fields of logical processors residing in 
	    // different packages.  Assuming the bit-masks are the same 
	    // on all processors in the system.
	    //
	    if (tblPkgID[procNum] == packageIDBucket[i])
	    {
		packageIDFound = 1;
		break;
	    }	    
	}
	
	// Did not match any bucket.  Create a new one.
	//
	if ( ! packageIDFound )   
	{
	    packageIDBucket[i] = tblPkgID[procNum];

	    // Total number of physical packages in the system
	    //
	    total++;		    
	}	
    }
    return total;
}


// Determine the total number of logical processors, processor
// cores and physical packages.
//
int CPUCount(int * totAvailLogical,
	     int * totAvailCore,
	     int * totPhysPack,
	     char * procData )
{
    unsigned int apicID;
    unsigned int numLPEnabled = 0;
    unsigned int tblPkgID[256], tblCoreID[256], tblSMTID[256];
    char tmp[256];

    *totAvailCore = 1;
    *totPhysPack  = 1;
    procData[0] = 0;

    // Determine the number of processors are available to run this process. 
    //
    int numProcessors = sysconf(_SC_NPROCESSORS_CONF); 
    
    // Get the system affinity mask.
    //
    cpu_set_t sysAffinityMask;	 
    misc_sched_getaffinity(0, &sysAffinityMask);
    
    for (int i = 0; i < numProcessors; i++ )
    {
	if ( CPU_ISSET(i, &sysAffinityMask) == 0 )
	    return USER_CONFIG_ISSUE;		
    }

    // Number of logical processors on each processor package.
    //
    int logicalPerPack = logicalProcessorsPerPackage();
	
    // Number of cores per processor package.
    //
    int corePerPack = multiCoresPerProcPak();

    if ( corePerPack <=  0 ) 
	return USER_CONFIG_ISSUE;

    // Number of logical processors on each core.  Assume that cores 
    // within a package have the same number of logical processors. 
    //
    int logicalPerCore = logicalPerPack / corePerPack;
    
    // Find the affinity and IDs of each logical processor in the
    // system by reading the initial APIC for each processor. 
    // 
    unsigned int affinityMask = 1;
    cpu_set_t currentCPU;
    unsigned int packageIDMask;
    int j = 0;

    while ( j < numProcessors )
    {
	CPU_ZERO(&currentCPU);
	CPU_SET(j, &currentCPU);
	
	if ( misc_sched_setaffinity (0, &currentCPU) == 0 )
	{
	    // Ensure system has switched to the right CPU
	    sleep(0);  
	    
	    // Get the initial APIC ID for this processor.
	    apicID = get_apic_id();
	    
	    // Obtain SMT ID and core ID of each logical processor from 
	    // initial APIC ID.  Shift value for SMT ID is 0.
	    // Shift value for core ID is the mask width for maximum logical
	    // processors per core
	    //
	    tblSMTID[j]  = getSubID(apicID, logicalPerCore, 0);
	    tblCoreID[j] = getSubID(apicID, corePerPack,
				    find_maskwidth(logicalPerCore));
	    
	    // Extract package ID from the initial APIC ID.
	    // Shift value is the mask width for max Logical per package
	    //
	    packageIDMask = (0xff << find_maskwidth( logicalPerPack ));
	    tblPkgID[j] = apicID & packageIDMask;
	    
	    // Number of available logical processors in the system.
	    //
	    numLPEnabled ++;   

	    // Hold results to print at end.
	    //
	    sprintf(tmp,
		    "AffinityMask = 0x%x; Initial APIC = 0x%x; Physical ID = %d, Core ID = %d,  SMT ID = %d\n",
		    affinityMask, apicID, tblPkgID[j], tblCoreID[j], 
		    tblSMTID[j]);
	    strcat(procData, tmp);
	} 
	
	j++;
	affinityMask = 1 << j;
    } 
    
    // restore the affinity setting to its original state
    //
    misc_sched_setaffinity (0, &sysAffinityMask);

    // The total number of logical processors enabled in the system.
    //
    *totAvailLogical = numLPEnabled;

    // Count the available cores in the system
    //
    *totAvailCore = countAvailableCores( tblPkgID, tblCoreID, numLPEnabled );  

    // Count the physical processors in the system
    //
    *totPhysPack = countPhysicalPacks( tblPkgID, numLPEnabled );

    
    //
    // Check to see if the system is multi-core or
    // contains hyper-threading technology.
    //
    if ( *totAvailCore > *totPhysPack ) 
    {
	// Multi-core
	if (logicalPerCore == 1)
	    return MULTI_CORE_AND_HT_NOT_CAPABLE;

	else if (numLPEnabled > *totAvailCore)
	    return MULTI_CORE_AND_HT_ENABLED;
	else 
	    return MULTI_CORE_AND_HT_DISABLED;
	
    }
    else
    {
	// Single-core
	if (logicalPerCore == 1)
	    return SINGLE_CORE_AND_HT_NOT_CAPABLE;

	else if (numLPEnabled > *totAvailCore)
	    return SINGLE_CORE_AND_HT_ENABLED;
	else 
	    return SINGLE_CORE_AND_HT_DISABLED;
    }
    
    return USER_CONFIG_ISSUE;
}


// Determine the topology of the system and print out the results.
//
int main(void)
{
    int totAvailLogical = 0;	// Number of logical CPUs per core
    int totAvailCore = 0;   	// Number of cores per processor package
    int totPhysPack  = 0;	// Total number of processor packages
    char procData[2048];	// Used for processor attributes
    
    // Determine the hyper-threading technology and multi-core support level.
    //
    int sysAttributes = CPUCount( &totAvailLogical, &totAvailCore, 
				  &totPhysPack, procData );

    int corePerPack = multiCoresPerProcPak();
    int logicalPerPack = logicalProcessorsPerPackage();

    assert (totPhysPack * corePerPack >= totAvailCore);
    assert (totPhysPack * logicalPerPack >= totAvailLogical);

    int logicalPerCore = logicalPerPack / corePerPack;

    printHeader();    
    printAttributes( sysAttributes, totPhysPack );
    printCapabilities( totPhysPack, totAvailCore, totAvailLogical, corePerPack,
		       logicalPerCore );
    printAffinity( procData );
    
    return 0;
}


// Print out general information at the top.
//
void printHeader()
{
    printf("\n----Counting Hardware Multi-threading Capabilities and Availability ---------- \n\n");
    printf("This application displays information on three forms of hardware multi-threading\n");
    printf("capability and availability. The three forms of capabilities are:\n");
    printf("multi-processor (MP), multi-core (core), and Hyper-Threading Technology (HT).\n");
    printf("\nHardware capability results represent the maximum number provided in hardware.\n");	
    printf("Note, Bios/OS or an experienced user can make configuration changes resulting in \n");
    printf("less-than-full hardware capabilities being available to applications.\n");
    printf("For the best result, the operator is responsible for configuring the BIOS/OS such that\n");
    printf("full hardware multi-threading capabilities are enabled.\n");
    printf("\n---------------------------------------------------------------------------- \n\n\n");
}


// Print the attributes of the processor.
//
void printAttributes( int sysAttributes, int totPhysPack )
{
    printf("\nCapabilities:\n\n");
    
    switch( sysAttributes )
    {	
    case MULTI_CORE_AND_HT_NOT_CAPABLE:
	printf("\tHyper-Threading Technology: Not capable  \n\tMulti-core: Yes \n\tMulti-processor: ");
	break;
	
    case SINGLE_CORE_AND_HT_NOT_CAPABLE:
	printf("\tHyper-Threading Technology: Not capable  \n\tMulti-core: No \n\tMulti-processor: ");
	break;
	
    case SINGLE_CORE_AND_HT_DISABLED:
	printf("\tHyper-Threading Technology: Disabled  \n\tMulti-core: No \n\tMulti-processor: ");
	
    case SINGLE_CORE_AND_HT_ENABLED:
	printf("\tHyper-Threading Technology: Enabled  \n\tMulti-core: No \n\tMulti-processor: ");
	break;
	
    case MULTI_CORE_AND_HT_DISABLED:
	printf("\tHyper-Threading Technology: Disabled  \n\tMulti-core: Yes \n\tMulti-processor: ");
	break;
	
    case MULTI_CORE_AND_HT_ENABLED:
	printf("\tHyper-Threading Technology: Enabled  \n\tMulti-core: Yes \n\tMulti-processor: ");
	break;
	
    case USER_CONFIG_ISSUE:
	printf("User Configuration Error: Not all logical processors in the system are enabled \
	while running this process. Please rerun this application after making corrections. \n");
	exit(1);
	break;
	
    default:
	printf("Error: Unexpected return value.\n");
	exit(1);
	
    }
    if (totPhysPack > 1) printf("Yes\n"); else printf("No\n");
    
}


// Print the system's capabilities.
//
void printCapabilities( int totPhysPack, int totAvailCore, int totAvailLogical,
			int corePerPack, int logicalPerPack )
{
    printf("\n\nHardware capability and its availability to applications: \n");
    printf("\n  System wide availability: %d physical processors, %d cores, %d logical processors\n", \
	   totPhysPack, totAvailCore, totAvailLogical);
    
    printf("  Multi-core capabililty : %d cores per package \n", corePerPack );
    printf("  HT capability: %d logical processors per core \n", logicalPerPack );
    
    if ( totPhysPack * corePerPack > totAvailCore) 
	printf("\n  Not all cores in the system are enabled for this application.\n");
    else 
	printf("\n  All cores in the system are enabled for this application.\n");
}


// Print out the affinity of each processor and its IDs.
//
void printAffinity( char procData[] )
{
    printf("\n\nRelationships between OS affinity mask, Initial APIC ID, and 3-level sub-IDs: \n");
    printf("\n%s\n\n", procData);
}


#define NOMINMAX
#include "StackTrace/Utilities.h"
#include "StackTrace/ErrorHandlers.h"
#include "StackTrace/StackTrace.h"
#include "StackTrace/Utilities.hpp"

#include <algorithm>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <typeinfo>

#ifdef USE_TIMER
    #include "MemoryApp.h"
#endif

#ifdef USE_GCOV
extern "C" void __gcov_dump();
#endif


#define perr std::cerr


// Detect the OS
// clang-format off
#if defined( WIN32 ) || defined( _WIN32 ) || defined( WIN64 ) || defined( _WIN64 ) || defined( _MSC_VER )
    #define USE_WINDOWS
#elif defined( __APPLE__ )
    #define USE_MAC
#elif defined( __linux ) || defined( __linux__ ) || defined( __unix ) || defined( __posix )
    #define USE_LINUX
    #define USE_NM
#else
    #error Unknown OS
#endif
// clang-format on


// Include system dependent headers
// clang-format off
#ifdef USE_WINDOWS
    #include <process.h>
    #include <psapi.h>
    #include <stdio.h>
    #include <tchar.h>
    #include <windows.h>
#else
    #include <dlfcn.h>
    #include <execinfo.h>
    #include <sched.h>
    #include <sys/time.h>
    #include <ctime>
    #include <unistd.h>
#endif
#ifdef USE_LINUX
    #include <malloc.h>
#endif
#ifdef USE_MAC
    #include <mach/mach.h>
    #include <sys/sysctl.h>
    #include <sys/types.h>
#endif
// clang-format on


#ifdef __GNUC__
    #define USE_ABI
    #include <cxxabi.h>
#endif


namespace StackTrace {


/****************************************************************************
 *  Function to find an entry                                                *
 ****************************************************************************/
template<class TYPE>
inline size_t findfirst( const std::vector<TYPE> &X, TYPE Y )
{
    if ( X.empty() )
        return 0;
    size_t lower = 0;
    size_t upper = X.size() - 1;
    if ( X[lower] >= Y )
        return lower;
    if ( X[upper] < Y )
        return upper;
    while ( ( upper - lower ) != 1 ) {
        size_t value = ( upper + lower ) / 2;
        if ( X[value] >= Y )
            upper = value;
        else
            lower = value;
    }
    return upper;
}


/****************************************************************************
 *  Function to terminate the program                                        *
 ****************************************************************************/
static bool abort_throwException = false;
static int force_exit            = 0;
void Utilities::setAbortBehavior( bool throwException, int stackType )
{
    abort_throwException = throwException;
    StackTrace::setDefaultStackType( static_cast<printStackType>( stackType ) );
}
void Utilities::abort( const std::string &message, const source_location &source )
{
    abort_error err;
    err.message   = message;
    err.source    = source;
    err.type      = terminateType::abort;
    err.bytes     = Utilities::getMemoryUsage();
    err.stackType = StackTrace::getDefaultStackType();
    err.stack     = StackTrace::backtrace();
    throw err;
}
static std::mutex terminate_mutex;
[[noreturn]] static inline void callAbort()
{
#ifdef USE_GCOV
    __gcov_dump();
#endif
    terminate_mutex.unlock();
    std::abort();
}
void Utilities::terminate( const StackTrace::abort_error &err )
{
    // Lock mutex to ensure multiple threads do not try to abort simultaneously
    terminate_mutex.lock();
    // Clear the error handlers
    clearErrorHandler();
    // Print the message and abort
    if ( force_exit > 1 ) {
        callAbort();
    } else if ( !abort_throwException ) {
        // Use MPI_abort (will terminate all processes)
        force_exit = 2;
        perr << err.what();
#if defined( USE_MPI ) || defined( HAVE_MPI )
        int initialized = 0, finalized = 0;
        MPI_Initialized( &initialized );
        MPI_Finalized( &finalized );
        if ( initialized != 0 && finalized == 0 ) {
            clearMPIErrorHandler( MPI_COMM_WORLD );
            MPI_Abort( MPI_COMM_WORLD, -1 );
        }
#endif
        callAbort();
    } else {
        perr << err.what();
        perr.flush();
        callAbort();
    }
}


/****************************************************************************
 *  Functions to set the error handler                                       *
 ****************************************************************************/
void Utilities::setErrorHandlers( std::function<void( StackTrace::abort_error & )> abort )
{
#ifdef USE_MPI
    setMPIErrorHandler( MPI_COMM_WORLD );
    setMPIErrorHandler( MPI_COMM_SELF );
#endif
    if ( abort )
        StackTrace::setErrorHandler( abort );
    else
        StackTrace::setErrorHandler( Utilities::terminate );
}
void Utilities::clearErrorHandlers()
{
#ifdef USE_MPI
    clearMPIErrorHandler( MPI_COMM_WORLD );
    clearMPIErrorHandler( MPI_COMM_SELF );
#endif
}


/****************************************************************************
 *  Function to get the memory usage                                         *
 *  Note: this function should be thread-safe                                *
 ****************************************************************************/
// clang-format off
#if defined( USE_MAC ) || defined( USE_LINUX )
    // Get the page size on mac or linux
    static size_t page_size = static_cast<size_t>( sysconf( _SC_PAGESIZE ) );
#endif
size_t Utilities::getSystemMemory()
{
    #if defined( USE_LINUX )
        static long pages = sysconf( _SC_PHYS_PAGES );
        size_t N_bytes    = pages * page_size;
    #elif defined( USE_MAC )
        int mib[2]    = { CTL_HW, HW_MEMSIZE };
        u_int namelen = sizeof( mib ) / sizeof( mib[0] );
        uint64_t size;
        size_t len = sizeof( size );
        size_t N_bytes = 0;
        if ( sysctl( mib, namelen, &size, &len, nullptr, 0 ) == 0 )
            N_bytes = size;
    #elif defined( USE_WINDOWS )
        MEMORYSTATUSEX status;
        status.dwLength = sizeof( status );
        GlobalMemoryStatusEx( &status );
        size_t N_bytes = status.ullTotalPhys;
    #else
        #error Unknown OS
    #endif
    return N_bytes;
}
size_t Utilities::getMemoryUsage()
{
    #ifdef USE_TIMER
        size_t N_bytes = MemoryApp::getTotalMemoryUsage();
    #else
        #if defined( WIN32 ) || defined( _WIN32 ) || defined( WIN64 ) || defined( _WIN64 )
            // Windows
            PROCESS_MEMORY_COUNTERS memCounter;
            GetProcessMemoryInfo( GetCurrentProcess(), &memCounter, sizeof( memCounter ) );
            size_t N_bytes = memCounter.WorkingSetSize;
        #elif defined( __APPLE__ )
            // MAC
            struct task_basic_info t_info;
            mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
            kern_return_t rtn =
                task_info( mach_task_self(), TASK_BASIC_INFO, (task_info_t) &t_info, &t_info_count );
            if ( rtn != KERN_SUCCESS )
                return 0;
            size_t N_bytes = t_info.virtual_size;
        #elif defined( HAVE_MALLINFO2 )
            // Linux - mallinfo2
            auto meminfo   = mallinfo2();
            size_t N_bytes = meminfo.hblkhd + meminfo.uordblks;
        #else
            // Linux - Deprecated mallinfo
            auto meminfo = mallinfo();
            size_t size_hblkhd   = static_cast<unsigned int>( meminfo.hblkhd );
            size_t size_uordblks = static_cast<unsigned int>( meminfo.uordblks );
            size_t N_bytes       = size_hblkhd + size_uordblks;
        #endif
    #endif
    return N_bytes;
}
// clang-format on


/****************************************************************************
 *  Functions to get the time and timer resolution                           *
 ****************************************************************************/
#if defined( USE_WINDOWS )
double Utilities::time()
{
    LARGE_INTEGER end, f;
    QueryPerformanceFrequency( &f );
    QueryPerformanceCounter( &end );
    double time = ( (double) end.QuadPart ) / ( (double) f.QuadPart );
    return time;
}
double Utilities::tick()
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency( &f );
    double resolution = ( (double) 1.0 ) / ( (double) f.QuadPart );
    return resolution;
}
#elif defined( USE_LINUX ) || defined( USE_MAC )
double Utilities::time()
{
    timeval current_time;
    gettimeofday( &current_time, nullptr );
    double time = ( (double) current_time.tv_sec ) + 1e-6 * ( (double) current_time.tv_usec );
    return time;
}
double Utilities::tick()
{
    timeval start, end;
    gettimeofday( &start, nullptr );
    gettimeofday( &end, nullptr );
    while ( end.tv_sec == start.tv_sec && end.tv_usec == start.tv_usec )
        gettimeofday( &end, nullptr );
    double resolution = ( (double) ( end.tv_sec - start.tv_sec ) ) +
                        1e-6 * ( (double) ( end.tv_usec - start.tv_usec ) );
    return resolution;
}
#else
    #error Unknown OS
#endif


/****************************************************************************
 *  Cause a segfault                                                         *
 ****************************************************************************/
void Utilities::cause_segfault()
{
    int *ptr = nullptr;
    ptr[0]   = 0;
}


/****************************************************************************
 *  Utility to call system command and return output                         *
 ****************************************************************************/
std::string Utilities::exec( const std::string &cmd, int &code )
{
    std::string result;
    auto fun = [&result]( const char *line ) { result += line; };
    code     = exec2( cmd.data(), fun );
    return result;
}


/****************************************************************************
 *  Get the type name                                                        *
 ****************************************************************************/
std::string Utilities::getTypeName( const std::type_info &id )
{
    std::string name = id.name();
#if defined( USE_ABI )
    int status;
    auto tmp = abi::__cxa_demangle( name.c_str(), 0, 0, &status );
    name.assign( tmp );
    free( tmp );
#endif
    return name;
}


} // namespace StackTrace

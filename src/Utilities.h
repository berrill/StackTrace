#ifndef included_Utilities
#define included_Utilities


#include <iostream>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <thread>
#include <vector>


namespace Utilities {

/*!
 * Aborts the run after printing an error message with file and
 * linenumber information.
 */
void abort( const std::string &message, const std::string &filename, const int line );


/*!
 * Set the behavior of abort
 * @param printMemory       Print the current memory usage (default is true)
 * @param printStack        Print the current call stack (default is true)
 * @param throwException    Throw an exception instead of MPI_Abort (default is false)
 * @param printOnAbort      Print the call stack when abort is called (default is false)
 * @param stackType         Type of stack to get (1: thread local stack, 2: all threads, 3: global)
 */
void setAbortBehavior( bool printMemory, bool printStack, bool throwException, bool printOnAbort=false, int stackType=2 );


//! Function to set the error handlers
void setErrorHandlers();


/*!
 * Set an environmental variable
 * @param name              The name of the environmental variable
 * @param value             The value to set
 */
void setenv( const char *name, const char *value );


/*!
 * Function to get the memory availible.
 * This function will return the total memory availible
 * Note: depending on the implimentation, this number may be rounded to
 * to a multiple of the page size.
 * If this function fails, it will return 0.
 */
size_t getSystemMemory();


/*!
 * Function to get the memory usage.
 * This function will return the total memory used by the application.
 * Note: depending on the implimentation, this number may be rounded to
 * to a multiple of the page size.
 * If this function fails, it will return 0.
 */
size_t getMemoryUsage();


//! Function to get an arbitrary point in time
double time();


//! Function to get the resolution of time
double tick();


/*!
 * Sleep for X ms
 * @param N         Time to sleep (ms)
 */
inline void sleep_ms( int N ) { std::this_thread::sleep_for( std::chrono::milliseconds( N ) ); }


/*!
 * Sleep for X s
 * @param N         Time to sleep (s)
 */
inline void sleep_s( int N ) { std::this_thread::sleep_for( std::chrono::seconds( N ) ); }


//! Cause a segfault
void cause_segfault();


/*!
 * @brief  Call system command
 * @details  This function calls a system command, waits for the program
 *   to execute, captures and returns the output and exit code.
 * @param[in] cmd           Command to execute
 * @param[out] exit_code    Exit code returned from child process
 * @return                  Returns string containing the output
 */
std::string exec( const std::string &cmd, int &exit_code );


} // namespace Utilities


#include "UtilityMacros.h"


#endif

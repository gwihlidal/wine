/*
 * DOS interrupt 20h handler (TERMINATE PROGRAM)
 */

#include <stdlib.h>
#include "miscemu.h"
/* #define DEBUG_INT */
#include "debug.h"
#include "task.h"

/**********************************************************************
 *	    INT_Int20Handler
 *
 * Handler for int 20h.
 */
void WINAPI INT_Int20Handler( CONTEXT *context )
{
        ExitProcess( 0 );
}

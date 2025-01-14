/*++ BUILD Version: 0001    // Increment this if a change has global effects

Copyright (c) 1991  Microsoft Corporation

Module Name:

    winsmibm.c

Abstract:

    Sample SNMP Extension Agent for Windows NT.

    These files (winsdll.c, winsmib.c, and winsmib.h) provide an example of 
    how to structure an Extension Agent DLL which works in conjunction with 
    the SNMP Extendible Agent for Windows NT.

    Extensive comments have been included to describe its structure and
    operation.  See also "Microsoft Windows/NT SNMP Programmer's Reference".

Created:

    28-Jun-1991

Revision History:

--*/


static char *vcsid = "@(#) $Logfile:   N:/xtest/vcs/winsdll.c_v  $ $Revision:   1.6  $";

#ifdef UNICODE
#undef UNICODE
#endif

// General notes:
//
//   Microsoft's Extendible Agent for Windows NT is implemented by dynamically
// linking to Extension Agent DLLs that implement portions of the MIB.  These
// Extension Agents are configured in the Windows NT Registration Database.
// When the Extendible Agent Service is started, it queries the registry to
// determine which Extension Agent DLLs have been installed and need to be
// loaded and initialized.  The Extendible Agent invokes various DLL entry
// points (examples follow in this file) to request MIB queries and obtain
// Extension Agent generated traps.


// Necessary includes.

#include <windows.h>
#include <malloc.h>
#include <tchar.h>
#include <snmp.h>


// Contains definitions for the table structure describing the MIB.  This
// is used in conjunction with winsmib.c where the MIB requests are resolved.

#include "winsmib.h"


// Extension Agent DLLs need access to elapsed time agent has been active.
// This is implemented by initializing the Extension Agent with a time zero
// reference, and allowing the agent to compute elapsed time by subtracting
// the time zero reference from the current system time.  This example
// Extension Agent implements this reference with dwTimeZero.

DWORD dwTimeZero = 0;


// Extension Agent DLLs that generate traps must create a Win32 Event object
// to communicate occurence of traps to the Extendible Agent.  The event
// handle is given to the Extendible Agent when the Extension Agent is 
// initialized, it should be NULL if traps will not be generated.  This
// example Extension Agent simulates the occurance of traps with hSimulateTrap.

HANDLE hSimulateTrap = NULL;

// The following variables are needed in order to figure out if the WINS service
// is installed and running on the box. If this is not true, the subagent should
// return SNMP_ERRORSTATUS_NOSUCHNAME for any query, instead of failing later with
// SNMP_ERRORSTATUS_GENERR.
#define WINS_SVC_NAME   _T("WINS")
SC_HANDLE hSvcController = NULL;
SC_HANDLE hWinsSvc = NULL;

// This call returns true if the WINS service is installed and running and
// false otherwise. If WINS is not running, the subagent should fail with
// SNMP_ERRORSTATUS_NOSUCHNAME or should return the first OID out of the MIB view
// instead of an SNMP_ERRORSTATUS_GENERR.
BOOL CheckWinsServiceUp(
    OUT AsnInteger *errorStatus)
{
    SERVICE_STATUS winsSvcStatus;

    // at first request, open the service controller handle
    if (hSvcController == NULL)
    {
        hSvcController = OpenSCManager(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
        // if unsuccessful this is a real genErr case (any app should be able to do so)
        if (hSvcController == NULL)
        {
            *errorStatus = SNMP_ERRORSTATUS_GENERR;
            return FALSE;
        }
    }

    // at first request, open the Wins service handle
    if (hWinsSvc == NULL)
    {
        hWinsSvc = OpenService(hSvcController, WINS_SVC_NAME, SERVICE_QUERY_STATUS);
        // if unsuccessful it might be that the service is not installed. In this case,
        // return noSuchName, otherwise there is a genErr case
        if (hWinsSvc == NULL)
        {
            *errorStatus = (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) ?
                                SNMP_ERRORSTATUS_NOSUCHNAME :
                                SNMP_ERRORSTATUS_GENERR;
            return FALSE;
        }
    }

    // we have both the service controller and the wins service handle, just query for the status
    if (!QueryServiceStatus(hWinsSvc, &winsSvcStatus))
    {
        // if querying the status of the service failed, this is a case of genErr
        *errorStatus = SNMP_ERRORSTATUS_GENERR;
        return FALSE;
    }


    // if WINS is in any other state than 'running', will handle as the service is stopped
    // and return NOSUCHNAME (allowing a possible MIB walk to continue)
    if (winsSvcStatus.dwCurrentState != SERVICE_RUNNING)
    {
       *errorStatus = SNMP_ERRORSTATUS_NOSUCHNAME;
       return FALSE;
    }

    // at this point the service is up and running
    *errorStatus = SNMP_ERRORSTATUS_NOERROR;
    return TRUE;
}

// This is a standard Win32 DLL entry point.  See the Win32 SDK for more
// information on its arguments and their meanings.  This example DLL does 
// not perform any special actions using this mechanism.

BOOL WINAPI DllMain(
    HANDLE hDll,
    DWORD  dwReason,
    LPVOID lpReserved)
    {
    switch(dwReason)
        {
        case DLL_PROCESS_ATTACH:
        case DLL_PROCESS_DETACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        default:
            break;

        } // end switch()

    return TRUE;

    } // end DllEntryPoint()


// Extension Agent DLLs provide the following entry point to coordinate the
// initializations of the Extension Agent and the Extendible Agent.  The
// Extendible Agent provides the Extension Agent with a time zero reference;
// and the Extension Agent provides the Extendible Agent with an Event handle 
// for communicating occurence of traps, and an object identifier representing
// the root of the MIB subtree that the Extension Agent supports.

BOOL SnmpExtensionInit(
    IN  DWORD               dwTimeZeroReference,
    OUT HANDLE              *hPollForTrapEvent,
    OUT AsnObjectIdentifier *supportedView)
    {

    // Record the time reference provided by the Extendible Agent.

    dwTimeZero = dwTimeZeroReference;


    // Create an Event that will be used to communicate the occurence of traps
    // to the Extendible Agent.  The Extension Agent will assert this Event
    // when a trap has occured.  This is explained further later in this file.

    if ((*hPollForTrapEvent = CreateEvent(NULL, FALSE, FALSE, NULL)) == NULL)
        {
        // Indicate error?, be sure that NULL is returned to Extendible Agent.
        }


    // Indicate the MIB view supported by this Extension Agent, an object
    // identifier representing the sub root of the MIB that is supported.

    *supportedView = MIB_OidPrefix; // NOTE!  structure copy


    // Record the trap Event.  This example Extension Agent simulates traps by 
    // generating a trap after every given number of processed requests.

    hSimulateTrap = *hPollForTrapEvent;

    WinsMibInit();

    // Indicate that Extension Agent initialization was sucessfull.

    return TRUE;

    } // end SnmpExtensionInit()


// Extension Agent DLLs provide the following entry point to communcate traps
// to the Extendible Agent.  The Extendible Agent will query this entry point
// when the trap Event (supplied at initialization time) is asserted, which
// indicates that zero or more traps may have occured.  The Extendible Agent 
// will repetedly call this entry point until FALSE is returned, indicating 
// that all outstanding traps have been processed.

BOOL SnmpExtensionTrap(
    OUT AsnObjectIdentifier *enterprise,
    OUT AsnInteger          *genericTrap,
    OUT AsnInteger          *specificTrap,
    OUT AsnTimeticks        *timeStamp,
    OUT RFC1157VarBindList  *variableBindings)
    {
    // The body of this routine is an extremely simple example/simulation of
    // the trap functionality.  A real implementation will be more complex.


    // The following define data inserted into the trap below.  The Lan Manager
    // bytesAvailAlert from the Lan Manager Alerts-2 MIB is generated with an
    // empty variable bindings list.

    static UINT OidList[]  = { 1, 3, 6, 1, 4, 1, 311, 1, 2 };
    static UINT OidListLen = 9;


    // The following variable is used for the simulation, it allows a single
    // trap to be generated and then causes FALSE to be returned indicating
    // no more traps.

    static whichTime = 0;


    // The following if/else support the simulation.

    if (whichTime == 0)
        {
        whichTime = 1;    // Supports the simulation.


        // Communicate the trap data to the Extendible Agent.

        enterprise->idLength = OidListLen;
        enterprise->ids = (UINT *)SNMP_malloc(sizeof(UINT) * OidListLen);
        memcpy(enterprise->ids, OidList, sizeof(UINT) * OidListLen);

        *genericTrap      = SNMP_GENERICTRAP_ENTERSPECIFIC;

        *specificTrap     = 1;                    // the bytesAvailAlert trap

        *timeStamp        = GetCurrentTime() - dwTimeZero;

        variableBindings->list = NULL;
        variableBindings->len  = 0;


        // Indicate that valid trap data exists in the parameters.

        return TRUE;
        }
    else
        {
        whichTime = 0;    // Supports the simulation.


        // Indicate that no more traps are available and parameters do not
        // refer to any valid data.

        return FALSE;
        }

    } // end SnmpExtensionTrap()


// Extension Agent DLLs provide the following entry point to resolve queries
// for MIB variables in their supported MIB view (supplied at initialization
// time).  The requestType is Get/GetNext/Set.

BOOL SnmpExtensionQuery(
    IN BYTE                   requestType,
    IN OUT RFC1157VarBindList *variableBindings,
    OUT AsnInteger            *errorStatus,
    OUT AsnInteger            *errorIndex)
{
    static unsigned long requestCount = 0;  // Supports the trap simulation.
    UINT    I;


//    EnterCriticalSection(&WinsMibCrtSec);
try {
    //
    // Iterate through the variable bindings list to resolve individual
    // variable bindings.
    //

    fWinsMibWinsStatusCnfCalled  = FALSE;
    fWinsMibWinsStatusStatCalled = FALSE;
    for ( I=0;I < variableBindings->len;I++ )
    {
        // resolve the variables only if WINS is up and running.
        if (CheckWinsServiceUp(errorStatus))
        {
            *errorStatus = ResolveVarBind( &variableBindings->list[I],
                                           requestType );
        }


	//
        // Test and handle case where Get Next past end of MIB view supported
        // by this Extension Agent occurs.  Special processing is required to 
        // communicate this situation to the Extendible Agent so it can take 
        // appropriate action, possibly querying other Extension Agents.
	//

        if ( *errorStatus == SNMP_ERRORSTATUS_NOSUCHNAME &&
             requestType == MIB_GETNEXT )
           {
           *errorStatus = SNMP_ERRORSTATUS_NOERROR;


           // Modify variable binding of such variables so the OID points
           // just outside the MIB view supported by this Extension Agent.
           // The Extendible Agent tests for this, and takes appropriate
           // action.

           SNMP_oidfree( &variableBindings->list[I].name );
           SNMP_oidcpy( &variableBindings->list[I].name, &MIB_OidPrefix );
           variableBindings->list[I].name.ids[MIB_PREFIX_LEN-1] ++;
           }


        // If an error was indicated, communicate error status and error
        // index to the Extendible Agent.  The Extendible Agent will ensure
        // that the origional variable bindings are returned in the response
        // packet.

        if ( *errorStatus != SNMP_ERRORSTATUS_NOERROR )
        {
	   *errorIndex = I+1;
	//   goto Exit;
	}
	else
	{
		*errorIndex = 0;
	}
   }

} // end of try
except(EXCEPTION_EXECUTE_HANDLER) {
	//
	// for now do nothing
	//	
	}
   // LeaveCriticalSection(&WinsMibCrtSec);

#if 0
    // Supports the trap simulation.

    if (++requestCount % 3 == 0 && hSimulateTrap != NULL)
        SetEvent(hSimulateTrap);


    // Indicate that Extension Agent processing was sucessfull.


#if 0
      if (*errorStatus != SNMP_ERRORSTATUS_NOERROR)
      {
	 return(FALSE);
      }	

    return TRUE;
#endif
#endif
    if (fWinsMibWinsKeyOpen)
    {
	RegCloseKey(WinsMibWinsKey);
	fWinsMibWinsKeyOpen = FALSE;
    }
    return SNMPAPI_NOERROR;

} // end SnmpExtensionQuery()

VOID
SnmpExtensionClose()
{
    // close the Wins service handle if it was previously opened
    if (hWinsSvc != NULL)
    {
        CloseServiceHandle(hWinsSvc);
    }

    // close the service controller handle if it was previously opened
    if (hSvcController != NULL)
    {
        CloseServiceHandle(hSvcController);
    }
}

//-------------------------------- END --------------------------------------


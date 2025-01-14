//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//
//  Copyright (C) Microsoft Corporation, 1997 - 2000
//
//  File:       miscutil.c
//
//--------------------------------------------------------------------------

#include "HotPlug.h"
#include <initguid.h>
#include <ntddstor.h>
#include <wdmguid.h>

LPTSTR
FormatString(
    LPCTSTR format,
    ...
    )
{
    LPTSTR str = NULL;
    va_list arglist;
    va_start(arglist, format);

    if (FormatMessage(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                      format,
                      0,
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                      (LPTSTR)&str,
                      0,
                      &arglist
                      ) == 0) {

        str = NULL;
    }

    va_end(arglist);

    return str;
}

PTCHAR
BuildLocationInformation(
    DEVINST DevInst,
    HMACHINE hMachine
    )
{
    CONFIGRET ConfigRet;
    ULONG ulSize;
    DWORD UINumber;
    PTCHAR Location = NULL;
    PTCHAR ParentName;
    DEVINST DevInstParent;
    int BusLocationStringId;
    TCHAR szBuffer[MAX_PATH];
    TCHAR UINumberDescFormat[MAX_PATH];
    TCHAR szFormat[MAX_PATH];
    HKEY hKey;
    DWORD Type = REG_SZ;

    szBuffer[0] = TEXT('\0');


    //
    // We will first get any LocationInformation for the device.  This will either
    // be in the LocationInformationOverride value in the devices driver (software) key
    // or if that is not present we will look for the LocationInformation value in
    // the devices device (hardware) key.
    //
    ulSize = sizeof(szBuffer);
    if (CR_SUCCESS == CM_Open_DevNode_Key_Ex(DevInst,
                                             KEY_READ,
                                             0,
                                             RegDisposition_OpenExisting,
                                             &hKey,
                                             CM_REGISTRY_SOFTWARE,
                                             hMachine
                                             )) {

        RegQueryValueEx(hKey,
                        REGSTR_VAL_LOCATION_INFORMATION_OVERRIDE,
                        NULL,
                        &Type,
                        (const PBYTE)szBuffer,
                        &ulSize
                        );

        RegCloseKey(hKey);
    }

    //
    // If the buffer is empty then we didn't get the LocationInformationOverride
    // value in the device's software key.  So, we will see if their is a
    // LocationInformation value in the device's hardware key.
    //
    if (szBuffer[0] == TEXT('\0')) {

        //
        // Get the LocationInformation for this device.
        //
        ulSize = sizeof(szBuffer);
        CM_Get_DevNode_Registry_Property_Ex(DevInst,
                                            CM_DRP_LOCATION_INFORMATION,
                                            NULL,
                                            szBuffer,
                                            &ulSize,
                                            0,
                                            hMachine
                                            );
    }

    //
    // UINumber has precedence over all other location information so check if this
    // device has a UINumber
    //
    ulSize = sizeof(UINumber);
    if ((CM_Get_DevNode_Registry_Property_Ex(DevInst,
                                             CM_DRP_UI_NUMBER,
                                             NULL,
                                             &UINumber,
                                             &ulSize,
                                             0,
                                             hMachine
                                             ) == CR_SUCCESS) &&
        (ulSize > 0)) {

        UINumberDescFormat[0] = TEXT('\0');
        ulSize = sizeof(UINumberDescFormat);

        //
        // Get the UINumber description format string from the device's parent,
        // if there is one, otherwise default to 'Location %1'
        if ((CM_Get_Parent_Ex(&DevInstParent, DevInst, 0, hMachine) == CR_SUCCESS) &&
            (CM_Get_DevNode_Registry_Property_Ex(DevInstParent,
                                                 CM_DRP_UI_NUMBER_DESC_FORMAT,
                                                 NULL,
                                                 UINumberDescFormat,
                                                 &ulSize,
                                                 0,
                                                 hMachine) == CR_SUCCESS) &&
            *UINumberDescFormat) {

        } else {

            LoadString(hHotPlug, IDS_UI_NUMBER_DESC_FORMAT, UINumberDescFormat, sizeof(UINumberDescFormat)/sizeof(TCHAR));
        }

        //
        // Prepend "at " to the begining of the UINumber string.
        //
        LoadString(hHotPlug, IDS_AT, szFormat, sizeof(szFormat)/sizeof(TCHAR));
        lstrcat(szFormat, UINumberDescFormat);

        //
        // Fill in the UINumber string
        //
        Location = FormatString(szFormat, UINumber);
    }

    //
    // We don't have a UINumber but we do have LocationInformation
    //
    else if (*szBuffer) {

        LoadString(hHotPlug, IDS_LOCATION, szFormat, sizeof(szFormat)/sizeof(TCHAR));
        Location = (PTCHAR)LocalAlloc(LPTR, lstrlen(szBuffer)*sizeof(TCHAR) + sizeof(szFormat) + sizeof(TCHAR));

        if (Location) {

            wsprintf(Location, szFormat, szBuffer);
        }
    }

    //
    // We don't have a UINumber or LocationInformation so we need to get a description
    // of the parent of this device.
    //
    else {

        ConfigRet = CM_Get_Parent_Ex(&DevInstParent, DevInst, 0, hMachine);
        if (ConfigRet == CR_SUCCESS) {

            //
            // Try the registry for FRIENDLYNAME
            //

            ulSize = sizeof(szBuffer);
            ConfigRet = CM_Get_DevNode_Registry_Property_Ex(DevInstParent,
                                                            CM_DRP_FRIENDLYNAME,
                                                            NULL,
                                                            szBuffer,
                                                            &ulSize,
                                                            0,
                                                            hMachine
                                                            );
            if (ConfigRet != CR_SUCCESS || !*szBuffer) {
                //
                // Try the registry for DEVICEDESC
                //

                ulSize = sizeof(szBuffer);
                ConfigRet = CM_Get_DevNode_Registry_Property_Ex(DevInstParent,
                                                                CM_DRP_DEVICEDESC,
                                                                NULL,
                                                                szBuffer,
                                                                &ulSize,
                                                                0,
                                                                hMachine
                                                                );
                if (ConfigRet != CR_SUCCESS || !*szBuffer) {

                    GUID ClassGuid;


                    //
                    // Initialize ClassGuid to GUID_NULL
                    //
                    CopyMemory(&ClassGuid,
                               &GUID_NULL,
                               sizeof(GUID)
                               );

                    //
                    // Try the registry for CLASSNAME
                    //
                    ulSize = sizeof(szBuffer);
                    ConfigRet = CM_Get_DevNode_Registry_Property_Ex(DevInstParent,
                                                                    CM_DRP_CLASSGUID,
                                                                    NULL,
                                                                    szBuffer,
                                                                    &ulSize,
                                                                    0,
                                                                    hMachine
                                                                    );


                    if (ConfigRet == CR_SUCCESS) {

                        pSetupGuidFromString(szBuffer, &ClassGuid);
                    }


                    if (!IsEqualGUID(ClassGuid, GUID_NULL) &&
                        !IsEqualGUID(ClassGuid, GUID_DEVCLASS_UNKNOWN))
                    {
                        ulSize = sizeof(szBuffer);
                        ConfigRet = CM_Get_DevNode_Registry_Property_Ex(DevInstParent,
                                                                        CM_DRP_CLASS,
                                                                        NULL,
                                                                        szBuffer,
                                                                        &ulSize,
                                                                        0,
                                                                        hMachine
                                                                        );
                    }

                    else {

                        ConfigRet = ~CR_SUCCESS;
                    }


                }
            }
        }

        if (*szBuffer) {

            LoadString(hHotPlug, IDS_LOCATION_NOUINUMBER, szFormat, sizeof(szFormat)/sizeof(TCHAR));
            Location = (PTCHAR)LocalAlloc(LPTR, lstrlen(szBuffer)*sizeof(TCHAR) + sizeof(szFormat) + sizeof(TCHAR));

            if (Location) {

                wsprintf(Location, szFormat, szBuffer);
            }
        }
    }

    return Location;
}

PTCHAR
BuildFriendlyName(
   DEVINST DevInst,
   HMACHINE hMachine
   )
{
    PTCHAR FriendlyName;
    CONFIGRET ConfigRet;
    ULONG ulSize;
    TCHAR szBuffer[MAX_PATH];

    //
    // Try the registry for FRIENDLYNAME
    //

    ulSize = sizeof(szBuffer);
    ConfigRet = CM_Get_DevNode_Registry_Property_Ex(DevInst,
                                                    CM_DRP_FRIENDLYNAME,
                                                    NULL,
                                                    szBuffer,
                                                    &ulSize,
                                                    0,
                                                    hMachine
                                                    );
    if (ConfigRet != CR_SUCCESS || !*szBuffer) {
        //
        // Try the registry for DEVICEDESC
        //

        ulSize = sizeof(szBuffer);
        ConfigRet = CM_Get_DevNode_Registry_Property_Ex(DevInst,
                                                        CM_DRP_DEVICEDESC,
                                                        NULL,
                                                        szBuffer,
                                                        &ulSize,
                                                        0,
                                                        hMachine
                                                        );
        if (ConfigRet != CR_SUCCESS || !*szBuffer) {
            GUID ClassGuid;

            //
            // Initialize ClassGuid to GUID_NULL
            //
            CopyMemory(&ClassGuid,
                       &GUID_NULL,
                       sizeof(GUID)
                       );

            //
            // Try the registry for CLASSNAME
            //

            ulSize = sizeof(szBuffer);
            ConfigRet = CM_Get_DevNode_Registry_Property_Ex(DevInst,
                                                            CM_DRP_CLASSGUID,
                                                            NULL,
                                                            szBuffer,
                                                            &ulSize,
                                                            0,
                                                            hMachine
                                                            );


            if (ConfigRet == CR_SUCCESS) {

                pSetupGuidFromString(szBuffer, &ClassGuid);
            }


            if (!IsEqualGUID(ClassGuid, GUID_NULL) &&
                !IsEqualGUID(ClassGuid, GUID_DEVCLASS_UNKNOWN))
            {
                ulSize = sizeof(szBuffer);
                ConfigRet = CM_Get_DevNode_Registry_Property_Ex(DevInst,
                                                                CM_DRP_CLASS,
                                                                NULL,
                                                                szBuffer,
                                                                &ulSize,
                                                                0,
                                                                hMachine
                                                                );
            }

            else {

                ConfigRet = ~CR_SUCCESS;
            }
        }
    }

    if (ConfigRet == CR_SUCCESS && *szBuffer) {

        FriendlyName = (PTCHAR)LocalAlloc(LPTR, ulSize);
        if (FriendlyName) {

            memcpy(FriendlyName, szBuffer, ulSize);
        }
    }

    else {

        FriendlyName = NULL;
    }


    return FriendlyName;
}





/* ----------------------------------------------------------------------
 * SetDlgText - Set Dialog Text Field
 *
 * Concatenates a number of string resources and does a SetWindowText()
 * for a dialog text control.
 *
 * Parameters:
 *
 *  hDlg         - Dialog handle
 *  iControl     - Dialog control ID to receive text
 *  nStartString - ID of first string resource to concatenate
 *  nEndString   - ID of last string resource to concatenate
 *
 *  Note: the string IDs must be consecutive.
 */

void
SetDlgText(HWND hDlg, int iControl, int nStartString, int nEndString)
{
    int     iX;
    TCHAR   szText[MAX_PATH];

    szText[0] = '\0';

    for (iX = nStartString; iX<= nEndString; iX++) {

        LoadString(hHotPlug,
                    iX,
                    szText + lstrlen(szText),
                    sizeof(szText)/sizeof(TCHAR) - lstrlen(szText)
                    );
    }

    if (iControl) {

        SetDlgItemText(hDlg, iControl, szText);
    }

    else {

        SetWindowText(hDlg, szText);
    }

}


VOID
HotPlugPropagateMessage(
    HWND hWnd,
    UINT uMessage,
    WPARAM wParam,
    LPARAM lParam
    )
{
    while ((hWnd = GetWindow(hWnd, GW_CHILD))) {

        SendMessage(hWnd, uMessage, wParam, lParam);
    }
}

BOOL
RemovalPermission(
   void
   )
{
    return TRUE;
}


int
HPMessageBox(
   HWND hWnd,
   int  IdText,
   int  IdCaption,
   UINT Type
   )
{
   TCHAR szText[MAX_PATH];
   TCHAR szCaption[MAX_PATH];

   if (LoadString(hHotPlug, IdText, szText, MAX_PATH) &&
       LoadString(hHotPlug, IdCaption, szCaption, MAX_PATH))
   {
       return MessageBox(hWnd, szText, szCaption, Type);
   }

   return IDIGNORE;
}



void
InvalidateTreeItemRect(
    HWND hwndTree,
    HTREEITEM  hTreeItem
    )
{
    RECT rect;

    if (hTreeItem && TreeView_GetItemRect(hwndTree, hTreeItem, &rect, FALSE)) {

        InvalidateRect(hwndTree, &rect, FALSE);
    }
}




DWORD
GetHotPlugFlags(
    PHKEY phKey
    )
{
    HKEY hKey;
    LONG Error;
    DWORD HotPlugFlags, cbHotPlugFlags;

    Error = RegCreateKey(HKEY_CURRENT_USER, REGSTR_PATH_SYSTRAY, &hKey);
    if (Error == ERROR_SUCCESS) {

        cbHotPlugFlags = sizeof(HotPlugFlags);

        Error = RegQueryValueEx(hKey,
                                szHotPlugFlags,
                                NULL,
                                NULL,
                                (LPBYTE)&HotPlugFlags,
                                &cbHotPlugFlags
                                );

        if (phKey) {

            *phKey = hKey;
        }

        else {

            RegCloseKey(hKey);
        }
    }

    if (Error != ERROR_SUCCESS) {

        HotPlugFlags = 0;
    }

    return HotPlugFlags;
}

//
// This function determines if the device is a boot storage device.
// We spit out a warning when users are trying to remove or disable
// a boot storage device(or a device contains a boot storage device).
//
// INPUT:
//  NONE
// OUTPUT:
//  TRUE  if the device is a boot device
//  FALSE if the device is not a boot device
LPTSTR
DevNodeToDriveLetter(
    DEVINST DevInst
    )
{
    ULONG ulSize;
    TCHAR szBuffer[MAX_PATH];
    TCHAR DeviceID[MAX_DEVICE_ID_LEN];
    PTSTR DriveString = NULL;
    PTSTR DeviceInterface = NULL;

    if (CM_Get_Device_ID_Ex(DevInst,
                            DeviceID,
                            sizeof(DeviceID)/sizeof(TCHAR),
                            0,
                            NULL
                            ) != CR_SUCCESS) {

        return FALSE;
    }

    // create a device info list contains all the interface classed
    // exposed by this device.
    ulSize = 0;

    if ((CM_Get_Device_Interface_List_Size(&ulSize,
                                           (LPGUID)&VolumeClassGuid,
                                           DeviceID,
                                           0)  == CR_SUCCESS) &&
        (ulSize > 1) &&
        ((DeviceInterface = (PTSTR)LocalAlloc(LPTR, ulSize*sizeof(TCHAR))) != NULL) &&
        (CM_Get_Device_Interface_List((LPGUID)&VolumeClassGuid,
                                      DeviceID,
                                      DeviceInterface,
                                      ulSize,
                                      0
                                      )  == CR_SUCCESS) &&
        *DeviceInterface)
    {
        PTSTR devicePath, p;
        TCHAR thisVolumeName[MAX_PATH];
        TCHAR enumVolumeName[MAX_PATH];
        TCHAR driveName[4];
        ULONG length;
        BOOL bResult;

        length = lstrlen(DeviceInterface);
        devicePath = (PTSTR)LocalAlloc(LPTR, (length + 1) * sizeof(TCHAR) + sizeof(UNICODE_NULL));

        if (devicePath) {

            lstrcpyn(devicePath, DeviceInterface, length + 1);

            p = wcschr(&(devicePath[4]), TEXT('\\'));

            if (!p) {
                //
                // No refstring is present in the symbolic link; add a trailing
                // '\' char (as required by GetVolumeNameForVolumeMountPoint).
                //
                p = devicePath + length;
                *p = TEXT('\\');
            }

            p++;
            *p = UNICODE_NULL;

            thisVolumeName[0] = UNICODE_NULL;
            bResult = GetVolumeNameForVolumeMountPoint(devicePath,
                                                       thisVolumeName,
                                                       MAX_PATH
                                                       );
            LocalFree(devicePath);

            if (bResult && thisVolumeName[0]) {

                driveName[1] = TEXT(':');
                driveName[2] = TEXT('\\');
                driveName[3] = TEXT('\0');

                for (driveName[0] = TEXT('A'); driveName[0] <= TEXT('Z'); driveName[0]++) {

                    enumVolumeName[0] = TEXT('\0');

                    GetVolumeNameForVolumeMountPoint(driveName, enumVolumeName, MAX_PATH);

                    if (!lstrcmpi(thisVolumeName, enumVolumeName)) {

                        driveName[2] = TEXT('\0');

                        wsprintf(szBuffer, TEXT(" - (%s)"), driveName);

                        DriveString = (PTSTR)LocalAlloc(LPTR, (lstrlen(szBuffer) + 1) * sizeof(TCHAR));

                        if (DriveString) {

                            lstrcpy(DriveString, szBuffer);
                        }

                        break;
                    }
                }
            }
        }
    }

    if (DeviceInterface) {

        LocalFree(DeviceInterface);
    }

    return DriveString;
}

BOOL
IsHotPlugDevice(
    DEVINST DevInst,
    HMACHINE hMachine
    )
/**+

    A device is considered a HotPlug device if the following are TRUE:
        - has Capability CM_DEVCAP_REMOVABLE
        - does NOT have Capability CM_DEVCAP_SURPRISEREMOVALOK
        - does NOT have Capability CM_DEVCAP_DOCKDEVICE
        - must be started (have the DN_STARTED devnode flag)
            - unless has capability CM_DEVCAP_EJECTSUPPORTED
            - or unless has capability CM_DEVCAP_RAWDEVICEOK

Returns:
    TRUE if this is a HotPlug device
    FALSE if this is not a HotPlug device.

-**/
{
    DWORD Capabilities;
    DWORD Len;
    DWORD Status, Problem;

    Capabilities = Status = Problem = 0;

    Len = sizeof(Capabilities);

    if (CM_Get_DevNode_Registry_Property_Ex(DevInst,
                                            CM_DRP_CAPABILITIES,
                                            NULL,
                                            (PVOID)&Capabilities,
                                            &Len,
                                            0,
                                            hMachine) != CR_SUCCESS) {
        return FALSE;
    }

    if (CM_Get_DevNode_Status_Ex(&Status,
                                 &Problem,
                                 DevInst,
                                 0,
                                 hMachine) != CR_SUCCESS) {
        return FALSE;
    }

    //
    // If this device is not removable, or it is surprise removal ok, or
    // it is a dock device, then it is not a hotplug device.
    //
    if ((!(Capabilities & CM_DEVCAP_REMOVABLE)) ||
        (Capabilities & CM_DEVCAP_SURPRISEREMOVALOK) ||
        (Capabilities & CM_DEVCAP_DOCKDEVICE)) {

        return FALSE;
    }

    //
    // We won't consider a device to be a hotplug device if it is not started,
    // unless it is either RAW capabile or an eject capable device.
    //
    // The reason for this test is that a bus driver might set the
    // CM_DEVCAP_REMOVABLE capability, but if the PDO doesn't get loaded then
    // it can't set the CM_DEVCAP_SURPRISEREMOVALOK. So we won't trust the
    // CM_DEVCAP_REMOVABLE capability if the PDO is not started.
    //
    if ((!(Capabilities & CM_DEVCAP_EJECTSUPPORTED)) &&
        (!(Status & DN_STARTED))) {

        return FALSE;
    }

    return TRUE;
}

BOOL
OpenPipeAndEventHandles(
    IN  LPWSTR    szCmd,
    OUT LPHANDLE  lphHotPlugPipe,
    OUT LPHANDLE  lphHotPlugEvent
    )
{
    BOOL   status = FALSE;
    HANDLE hPipe  = INVALID_HANDLE_VALUE;
    HANDLE hEvent = NULL;
    ULONG  ulEventNameSize;
    WCHAR  szEventName[MAX_PATH];
    DWORD  dwBytesRead;


    __try {
        //
        // Validate supplied arguments.
        //
        if (!lphHotPlugPipe || !lphHotPlugEvent) {
            return FALSE;
        }

        //
        // Make sure that a named pipe was specified in the cmd line.
        //
        if(!szCmd || !*szCmd) {
            return FALSE;
        }

        //
        // Wait for the specified named pipe to become available from the server.
        //
        if (!WaitNamedPipe(szCmd,
                           180000) // BUGBUG-2000/07/10-jamesca:  How long should we wait?
                           ) {
            return FALSE;
        }

        //
        // Open a handle to the specified named pipe
        //
        hPipe = CreateFile(szCmd,
                           GENERIC_READ,
                           0,
                           NULL,
                           OPEN_EXISTING,
                           0,
                           NULL);
        if (hPipe == INVALID_HANDLE_VALUE) {
            return FALSE;
        }

        //
        // The very first thing in the pipe should be the size of the event name.
        //
        if (ReadFile(hPipe,
                     (LPVOID)&ulEventNameSize,
                     sizeof(ULONG),
                     &dwBytesRead,
                     NULL)) {

            ASSERT(ulEventNameSize != 0);
            if ((ulEventNameSize == 0) ||
                (ulEventNameSize > MAX_PATH)) {
                goto clean0;
            }

            //
            // The next thing in the pipe should be the name of the event.
            //
            if (!ReadFile(hPipe,
                          (LPVOID)&szEventName,
                          ulEventNameSize,
                          &dwBytesRead,
                          NULL)) {
                goto clean0;
            }

        } else {
            if (GetLastError() == ERROR_INVALID_HANDLE) {
                //
                // The handle to the named pipe is not valid.  Make sure we don't
                // try to close it on exit.
                //
                hPipe = INVALID_HANDLE_VALUE;
            }
            goto clean0;
        }

        //
        // Open a handle to the specified named event that we can set and wait on.
        //
        hEvent = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE,
                            FALSE,
                            szEventName);
        if (hEvent == NULL) {
            goto clean0;
        }

        //
        // We should now have valid handles to both the pipe and the event.
        //
        status = TRUE;
        ASSERT((hPipe != INVALID_HANDLE_VALUE) && hEvent);


    clean0:
        ;

    } __except(EXCEPTION_EXECUTE_HANDLER) {

        status = FALSE;
    }

    if (status) {

        *lphHotPlugPipe  = hPipe;
        *lphHotPlugEvent = hEvent;

    } else {

        if (hPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(hPipe);
        }
        if (hEvent) {
            CloseHandle(hEvent);
        }
    }

    return status;
}

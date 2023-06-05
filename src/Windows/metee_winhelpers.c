/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2014-2023 Intel Corporation
 */
#include <assert.h>
#include <windows.h>
#include <initguid.h>
#include "helpers.h"
#include "Public.h"
#include "metee.h"
#include "metee_win.h"
#include <cfgmgr32.h>
#include <Objbase.h>
#include <Devpkey.h>
#include <Strsafe.h>


/*********************************************************************
**                       Windows Helper Functions                   **
**********************************************************************/
void DebugPrint(const char* args, ...)
{
	char msg[DEBUG_MSG_LEN + 1];
	va_list varl;
	va_start(varl, args);
	vsprintf_s(msg, DEBUG_MSG_LEN, args, varl);
	va_end(varl);

#ifdef SYSLOG
	OutputDebugStringA(msg);
#else
	fprintf(stderr, "%s", msg);
#endif /* SYSLOG */
}

/*
**	Start Overlapped Operation
**
**	Parameters:
**
**	Return:
**		TEE_INVALID_PARAMETER
**		TEE_INTERNAL_ERROR
*/
TEESTATUS BeginOverlappedInternal(IN TEE_OPERATION operation, IN HANDLE handle,
		                  IN PVOID buffer, IN ULONG bufferSize, OUT PEVENTHANDLE evt)
{
	TEESTATUS       status;
	EVENTHANDLE     pOverlapped     = NULL;
	DWORD           bytesTransferred= 0;
	BOOLEAN         optSuccesed     = FALSE;

	FUNC_ENTRY();

	if (INVALID_HANDLE_VALUE == handle || NULL == buffer || 0 == bufferSize || NULL == evt) {
		status = TEE_INVALID_PARAMETER;
		ERRPRINT("One of the parameters was illegal");
		goto Cleanup;
	}

	// allocate overlapped struct
	pOverlapped = (EVENTHANDLE)MALLOC(sizeof(OVERLAPPED));
	if (NULL == pOverlapped) {
		status = TEE_INTERNAL_ERROR;
		ERRPRINT("Error in MALLOC, error: %d\n", GetLastError());
		goto Cleanup;
	}

	pOverlapped->hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (NULL == pOverlapped->hEvent) {
		status = TEE_INTERNAL_ERROR;
		ERRPRINT("Error in CreateEvent, error: %d\n", GetLastError());
		goto Cleanup;
	}


	if (operation == ReadOperation) {
		if (ReadFile(handle, buffer, bufferSize, &bytesTransferred,
			     (LPOVERLAPPED)pOverlapped)) {
			optSuccesed = TRUE;
		}
	}
	else if (operation == WriteOperation) {
		if (WriteFile(handle, buffer, bufferSize, &bytesTransferred,
			      (LPOVERLAPPED)pOverlapped)) {
			optSuccesed = TRUE;
		}
	}

	if (optSuccesed == FALSE) {
		DWORD err = GetLastError();

		if (ERROR_IO_PENDING != err) {
			status = Win32ErrorToTee(err);
			ERRPRINT("Error in ReadFile/Write, error: %d\n", err);
		}
		else {
			ERRPRINT("Pending in ReadFile/Write");
			status = TEE_SUCCESS;
		}
	}
	else {
		status = TEE_SUCCESS;
	}

Cleanup:
	if (TEE_SUCCESS != status) {
		if (pOverlapped) {
			if (pOverlapped->hEvent)
				CloseHandle(pOverlapped->hEvent);
			FREE(pOverlapped);
		}
	}
	else {
		*evt = (EVENTHANDLE)pOverlapped;
	}

	FUNC_EXIT(status);

	return status;

}

TEESTATUS EndOverlapped(IN HANDLE handle, IN EVENTHANDLE evt, IN DWORD milliseconds,
			OUT OPTIONAL LPDWORD pNumberOfBytesTransferred)
{
	TEESTATUS       status;
	DWORD           err;
	EVENTHANDLE     pOverlapped             = evt;
	DWORD           bytesTransferred        = 0;
	LPDWORD         pBytesTransferred       = NULL;

	FUNC_ENTRY();

	if (INVALID_HANDLE_VALUE == handle || NULL == evt) {
		status = TEE_INVALID_PARAMETER;
		ERRPRINT("One of the parameters was illegal\n");
		goto Cleanup;
	}

	pBytesTransferred = pNumberOfBytesTransferred ? pNumberOfBytesTransferred : &bytesTransferred;

	// wait for the answer
	err = WaitForSingleObject(evt->hEvent, milliseconds);
	if (err == WAIT_TIMEOUT) {
		status = TEE_TIMEOUT;
		ERRPRINT("WaitForSingleObject timed out!\n");
		goto Cleanup;
	}

	if (err != WAIT_OBJECT_0) {
		assert(WAIT_FAILED == err);
		err = GetLastError();
		status = Win32ErrorToTee(err);

		ERRPRINT("WaitForSingleObject reported error: %d\n", err);
		goto Cleanup;
	}

	 // last parameter is true b/c if we're here the operation has been completed)
	if (!GetOverlappedResult(handle, (LPOVERLAPPED)pOverlapped, pBytesTransferred, TRUE)) {
		err = GetLastError();
		status = Win32ErrorToTee(err);
		ERRPRINT("Error in GetOverlappedResult, error: %d\n", err);
		goto Cleanup;
	}

	status = TEE_SUCCESS; //not really needed, but for completeness...

Cleanup:
	if (pOverlapped) {
		if (pOverlapped->hEvent)
			CloseHandle(pOverlapped->hEvent);
		FREE(pOverlapped);
	}
	FUNC_EXIT(status);

	return status;
}

TEESTATUS EndReadInternal(IN HANDLE handle, IN EVENTHANDLE evt, DWORD milliseconds,
			  OUT OPTIONAL LPDWORD pNumberOfBytesRead)

{
	TEESTATUS status;

	FUNC_ENTRY();

	status = EndOverlapped(handle, evt, milliseconds, pNumberOfBytesRead);

	FUNC_EXIT(status);

	return status;
}

TEESTATUS BeginReadInternal(IN HANDLE handle,
			    IN PVOID buffer, IN ULONG bufferSize, OUT PEVENTHANDLE evt)

{
	TEESTATUS status;

	FUNC_ENTRY();

	status = BeginOverlappedInternal(ReadOperation ,handle, buffer, bufferSize, evt);

	FUNC_EXIT(status);

	return status;
}

TEESTATUS BeginWriteInternal(IN HANDLE handle,
			     IN const PVOID buffer, IN ULONG bufferSize, OUT PEVENTHANDLE evt)
{
	TEESTATUS status;

	FUNC_ENTRY();

	status = BeginOverlappedInternal(WriteOperation ,handle, buffer, bufferSize, evt);

	FUNC_EXIT(status);

	return status;
}

TEESTATUS EndWriteInternal(IN HANDLE handle, IN EVENTHANDLE evt, DWORD milliseconds,
			   OUT OPTIONAL LPDWORD pNumberOfBytesWritten)
{
	TEESTATUS status;

	FUNC_ENTRY();

	status = EndOverlapped(handle, evt, milliseconds, pNumberOfBytesWritten);

	FUNC_EXIT(status);

	return status;
}

/*
**	Return the given Device Path according to it's device GUID
**
**	Parameters:
**		InterfaceGuid - Device GUID
**		path - Device path buffer
**		pathSize - Device Path buffer size
**
**	Return:
**		TEE_DEVICE_NOT_FOUND
**		TEE_INVALID_PARAMETER
**		TEE_INTERNAL_ERROR
*/
TEESTATUS GetDevicePath(_In_ LPCGUID InterfaceGuid,
			_Out_writes_(pathSize) char *path, _In_ SIZE_T pathSize)
{
	CONFIGRET     cr;
	char         *deviceInterfaceList         = NULL;
	ULONG         deviceInterfaceListLength   = 0;
	HRESULT       hr                          = E_FAIL;
	TEESTATUS     status                      = TEE_INTERNAL_ERROR;

	FUNC_ENTRY();

	if (InterfaceGuid == NULL || path == NULL || pathSize < 1) {
		status = TEE_INTERNAL_ERROR;
		ERRPRINT("One of the parameters was illegal");
		goto Cleanup;
	}

	path[0] = 0x00;

	cr = CM_Get_Device_Interface_List_SizeA(
		&deviceInterfaceListLength,
		(LPGUID)InterfaceGuid,
		NULL,
		CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
	if (cr != CR_SUCCESS) {
		ERRPRINT("Error 0x%x retrieving device interface list size.\n", cr);
		status = TEE_INTERNAL_ERROR;
		goto Cleanup;
	}

	if (deviceInterfaceListLength <= 1) {
		status = TEE_DEVICE_NOT_FOUND;
		ERRPRINT("SetupDiGetClassDevs returned status %d", GetLastError());
		goto Cleanup;
	}

	deviceInterfaceList = (char*)malloc(deviceInterfaceListLength * sizeof(char));
	if (deviceInterfaceList == NULL) {
		ERRPRINT("Error allocating memory for device interface list.\n");
		status = TEE_INTERNAL_ERROR;
		goto Cleanup;
	}
	ZeroMemory(deviceInterfaceList, deviceInterfaceListLength * sizeof(char));

	cr = CM_Get_Device_Interface_ListA(
		(LPGUID)InterfaceGuid,
		NULL,
		deviceInterfaceList,
		deviceInterfaceListLength,
		CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
	if (cr != CR_SUCCESS) {
		ERRPRINT("Error 0x%x retrieving device interface list.\n", cr);
		status = TEE_INTERNAL_ERROR;
		goto Cleanup;
	}

	hr = StringCchCopyA(path, pathSize, deviceInterfaceList);
	if (FAILED(hr)) {
		status = TEE_INTERNAL_ERROR;
		ERRPRINT("Error: StringCchCopy failed with HRESULT 0x%x", hr);
		goto Cleanup;
	}

	status = TEE_SUCCESS;

Cleanup:
	if (deviceInterfaceList != NULL) {
		free(deviceInterfaceList);
	}

	FUNC_EXIT(status);

	return status;
}

TEESTATUS SendIOCTL(IN HANDLE handle, IN DWORD ioControlCode,
		    IN LPVOID pInBuffer, IN DWORD inBufferSize,
		    IN LPVOID pOutBuffer, IN DWORD outBufferSize, OUT LPDWORD pBytesRetuned)
{
	OVERLAPPED      overlapped = {0}; // it's OK to put the overlapped in the stack here
	TEESTATUS       status;
	DWORD           err;

	FUNC_ENTRY();

	if (INVALID_HANDLE_VALUE == handle || NULL == pBytesRetuned) {
		status = ERROR_INVALID_PARAMETER;
		ERRPRINT("One of the parameters was illegal");
		goto Cleanup;
	}

	overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (INVALID_HANDLE_VALUE == overlapped.hEvent) {
		err = GetLastError();
		ERRPRINT("Error in CreateEvent, error: %d\n", err);
		status = Win32ErrorToTee(err);
		goto Cleanup;
	}

	if (!DeviceIoControl(handle, ioControlCode,
			     pInBuffer, inBufferSize,
			     pOutBuffer, outBufferSize,
			     pBytesRetuned, &overlapped)) {

		err = GetLastError();
		// it's ok to get an error here, because it's overlapped
		if (ERROR_IO_PENDING != err) {
			ERRPRINT("Error in DeviceIoControl, error: %d\n", err);
			status = Win32ErrorToTee(err);
			goto Cleanup;
		}
	}


	if (!GetOverlappedResult(handle, &overlapped, pBytesRetuned, TRUE)) {
		err = GetLastError();
		ERRPRINT("Error in GetOverlappedResult, error: %d\n", err);
		status = Win32ErrorToTee(err);
		goto Cleanup;
	}

	status = TEE_SUCCESS;

Cleanup:
	if (overlapped.hEvent)
		CloseHandle(overlapped.hEvent);

	FUNC_EXIT(status);

	return status;
}

TEESTATUS Win32ErrorToTee(_In_ DWORD win32Error)
{
	switch (win32Error) {
	case ERROR_INVALID_HANDLE:
		return TEE_INVALID_PARAMETER;
	case ERROR_INSUFFICIENT_BUFFER:
		return TEE_INSUFFICIENT_BUFFER;
	case ERROR_GEN_FAILURE:
		return TEE_UNABLE_TO_COMPLETE_OPERATION;
	case ERROR_DEVICE_NOT_CONNECTED:
		return TEE_DEVICE_NOT_READY;
	case ERROR_NOT_FOUND:
		return TEE_CLIENT_NOT_FOUND;
	case ERROR_ACCESS_DENIED:
		return TEE_PERMISSION_DENIED;
	default:
		return TEE_INTERNAL_ERROR;
	}
}

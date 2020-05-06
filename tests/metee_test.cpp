/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2014-2020 Intel Corporation
 */
#include <vector>
#include <chrono>
#include <thread>
#include <climits>
#include "metee_test.h"
#ifdef WIN32
extern "C" {
#include "public.h"
#include "metee_win.h"
}
#endif // WIN32

DEFINE_GUID(GUID_NON_EXISTS_CLIENT,
	0x85eb8fa6, 0xbdd, 0x4d01, 0xbe, 0xc4, 0xa5, 0x97, 0x43, 0x4e, 0xd7, 0x62);


void TEEAPI CompletionRoutine(TEESTATUS status, size_t numberOfBytesTransfered)
{
	std::cout << "operation ended with status " << status << ". Num bytes transferred " << numberOfBytesTransfered << std::endl;
}
#define SUCCESS TEE_SUCCESS

// Retrieve the system error message for the last-error code
#ifdef WIN32
std::string GetErrorString(unsigned long LastError)
{

	LPVOID lpMsgBuf;
	FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_ARGUMENT_ARRAY |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		L"%0",
		LastError,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&lpMsgBuf,
		0, NULL);

	std::string ErrorMessage((LPCSTR)lpMsgBuf);
	ErrorMessage.erase(ErrorMessage.find('\n')); //error string from system comes with a new line character. 
	return ErrorMessage;
}
#else
std::string GetErrorString(unsigned long LastError)
{
	return strerror(LastError);
}
#endif // WIN32

/*
Send GetVersion Command to HCI / MKHI
1) Open Connection to MKHI
2) Send GetVersion Req Command
3) Receive GetVersion Resp Command
4) Check for Valid Resp
5) Close Connection
*/
TEST_P(MeTeeTEST, PROD_MKHI_SimpleGetVersion)
{
	TEEHANDLE Handle = TEEHANDLE_ZERO;
	size_t NumberOfBytes = 0;
	struct MeTeeTESTParams intf = GetParam();
	std::vector <char> MaxResponse;
	GEN_GET_FW_VERSION_ACK* pResponseMessage; //max length for this client is 2048
	TEESTATUS status;

	status = TestTeeInitGUID(&Handle, intf.client, intf.device);
	if (status == TEE_DEVICE_NOT_FOUND)
		GTEST_SKIP();
	ASSERT_EQ(SUCCESS, status);
	ASSERT_NE(TEE_INVALID_DEVICE_HANDLE, TeeGetDeviceHandle(&Handle));
	ASSERT_EQ(SUCCESS, TeeConnect(&Handle));


	MaxResponse.resize(Handle.maxMsgLen*sizeof(char));
	ASSERT_EQ(SUCCESS, TeeWrite(&Handle, &MkhiRequest, sizeof(GEN_GET_FW_VERSION), &NumberOfBytes, 0));
	ASSERT_EQ(sizeof(GEN_GET_FW_VERSION), NumberOfBytes);

	ASSERT_EQ(SUCCESS, TeeRead(&Handle, &MaxResponse[0], Handle.maxMsgLen, &NumberOfBytes, 0));
	pResponseMessage = (GEN_GET_FW_VERSION_ACK*)(&MaxResponse[0]);

	ASSERT_EQ(SUCCESS, pResponseMessage->Header.Fields.Result);
	EXPECT_NE(0, pResponseMessage->Data.FWVersion.CodeMajor);
	EXPECT_NE(0, pResponseMessage->Data.FWVersion.CodeBuildNo);
		
	TeeDisconnect(&Handle);
	EXPECT_EQ(TEE_INVALID_DEVICE_HANDLE, TeeGetDeviceHandle(&Handle));
}

/*
Wait for timeout on recv data without send
1) Open Connection to MKHI
2) Receive timeout on GetVersion Resp Command
3) Close Connection
*/
TEST_P(MeTeeTEST, PROD_MKHI_TimeoutGetVersion)
{
	TEEHANDLE Handle = TEEHANDLE_ZERO;
	size_t NumberOfBytes = 0;
	struct MeTeeTESTParams intf = GetParam();
	std::vector <char> MaxResponse;
	TEESTATUS status;

	status = TestTeeInitGUID(&Handle, intf.client, intf.device);
	if (status == TEE_DEVICE_NOT_FOUND)
		GTEST_SKIP();
	ASSERT_EQ(SUCCESS, status);
	ASSERT_NE(TEE_INVALID_DEVICE_HANDLE, TeeGetDeviceHandle(&Handle));
	ASSERT_EQ(SUCCESS, TeeConnect(&Handle));

	MaxResponse.resize(Handle.maxMsgLen*sizeof(char));

	EXPECT_EQ(TEE_TIMEOUT, TeeRead(&Handle, &MaxResponse[0], Handle.maxMsgLen, &NumberOfBytes, 1000));

	TeeDisconnect(&Handle);
	EXPECT_EQ(TEE_INVALID_DEVICE_HANDLE, TeeGetDeviceHandle(&Handle));
}

/*
Obtain FW status
1) Receive FW status
2) Check for Valid Resp
*/
TEST_P(MeTeeTEST, PROD_MKHI_GetFWStatus)
{
	TEEHANDLE Handle = TEEHANDLE_ZERO;
	uint32_t fwStatusNum;
	uint32_t fwStatus;
	struct MeTeeTESTParams intf = GetParam();
	TEESTATUS status;

	status = TestTeeInitGUID(&Handle, intf.client, intf.device);
	if (status == TEE_DEVICE_NOT_FOUND)
		GTEST_SKIP();
	ASSERT_EQ(TEE_SUCCESS, status);
	ASSERT_NE(TEE_INVALID_DEVICE_HANDLE, TeeGetDeviceHandle(&Handle));

	//FWSTS1
	fwStatusNum = 0;
	ASSERT_EQ(SUCCESS, TeeFWStatus(&Handle, fwStatusNum, &fwStatus));
	EXPECT_NE(0, fwStatus);

	//FWSTS2
	fwStatusNum = 1;
	ASSERT_EQ(SUCCESS, TeeFWStatus(&Handle, fwStatusNum, &fwStatus));
	EXPECT_NE(0, fwStatus);

	//Invalid input
	fwStatusNum = 6;
	ASSERT_EQ(TEE_INVALID_PARAMETER, TeeFWStatus(&Handle, fwStatusNum, &fwStatus));
	fwStatusNum = UINT_MAX;
	ASSERT_EQ(TEE_INVALID_PARAMETER, TeeFWStatus(&Handle, fwStatusNum, &fwStatus));
	fwStatusNum = 1;
	ASSERT_EQ(TEE_INVALID_PARAMETER, TeeFWStatus(NULL, fwStatusNum, &fwStatus));
	ASSERT_EQ(TEE_INVALID_PARAMETER, TeeFWStatus(&Handle, fwStatusNum, NULL));
	ASSERT_EQ(TEE_INVALID_PARAMETER, TeeFWStatus(NULL, fwStatusNum, NULL));

	TeeDisconnect(&Handle);
	EXPECT_EQ(TEE_INVALID_DEVICE_HANDLE, TeeGetDeviceHandle(&Handle));
}

TEST_P(MeTeeNTEST, PROD_N_TestConnectToNullUuid)
{
	TEEHANDLE handle = TEEHANDLE_ZERO;
	struct MeTeeTESTParams intf = GetParam();

	ASSERT_EQ(TEE_INVALID_PARAMETER, TestTeeInitGUID(&handle, NULL, intf.device));
}

TEST_P(MeTeeNTEST, PROD_N_TestConnectToNonExistsUuid)
{
	TEEHANDLE handle = TEEHANDLE_ZERO;
	struct MeTeeTESTParams intf = GetParam();
	TEESTATUS status;

	status = TestTeeInitGUID(&handle, &GUID_NON_EXISTS_CLIENT, intf.device);
	if (status == TEE_DEVICE_NOT_FOUND)
		GTEST_SKIP();
	ASSERT_EQ(TEE_SUCCESS, status);

	ASSERT_EQ(TEE_CLIENT_NOT_FOUND, TeeConnect(&handle));
}

TEST_P(MeTeeNTEST, PROD_N_TestLongDevicePath)
{
	TEEHANDLE handle = TEEHANDLE_ZERO;
	const TEE_PATH_CHAR *longPath = TEE_PATH_TEXT("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.");

#ifdef _WIN32
	ASSERT_EQ(TEE_DEVICE_NOT_FOUND, TeeInit(&handle, &GUID_NON_EXISTS_CLIENT, longPath));
#else /* _WIN32 */
	/* TODO: Linux code lose error code in this path */
	ASSERT_EQ(TEE_INTERNAL_ERROR, TeeInit(&handle, &GUID_NON_EXISTS_CLIENT, longPath));
#endif /* _WIN32 */
}

TEST_P(MeTeeNTEST, PROD_N_TestLongClientPath)
{
	TEEHANDLE handle = TEEHANDLE_ZERO;
	struct MeTeeTESTParams intf = GetParam();
	TEESTATUS status;
	const char *longPath = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";

	status = TestTeeInitGUID(&handle, (const GUID*)longPath, intf.device);
	if (status == TEE_DEVICE_NOT_FOUND)
		GTEST_SKIP();
	ASSERT_EQ(TEE_SUCCESS, status);

	ASSERT_EQ(TEE_CLIENT_NOT_FOUND, TeeConnect(&handle));
}

TEST_P(MeTeeNTEST, PROD_N_TestGetDriverVersion)
{
	TEEHANDLE handle = TEEHANDLE_ZERO;
	struct MeTeeTESTParams intf = GetParam();
	teeDriverVersion_t ver = {0, 0, 0, 0};
	TEESTATUS status;

	status = TestTeeInitGUID(&handle, &GUID_NON_EXISTS_CLIENT, intf.device);
	if (status == TEE_DEVICE_NOT_FOUND)
		GTEST_SKIP();
	ASSERT_EQ(TEE_SUCCESS, status);

#ifdef WIN32
	ASSERT_EQ(TEE_SUCCESS, GetDriverVersion(&handle, &ver));

	EXPECT_NE(ver.major, 0);
	EXPECT_NE(ver.minor, 0);
	//hotfix may be 0. such as 99.13.0.x
	EXPECT_NE(ver.build, 0);
#else // WIN32
	ASSERT_EQ(TEE_NOTSUPPORTED, GetDriverVersion(&handle, &ver));
#endif // WIN32
}

TEST_P(MeTeeNTEST, PROD_N_TestGetDriverVersion_NullParam)
{
	TEEHANDLE handle = TEEHANDLE_ZERO;
	struct MeTeeTESTParams intf = GetParam();
	TEESTATUS status;

	status = TestTeeInitGUID(&handle, &GUID_NON_EXISTS_CLIENT, intf.device);
	if (status == TEE_DEVICE_NOT_FOUND)
		GTEST_SKIP();
	ASSERT_EQ(TEE_SUCCESS, status);

	ASSERT_EQ(TEE_INVALID_PARAMETER, GetDriverVersion(&handle, NULL));
}

#ifdef WIN32
TEST_P(MeTeeNTEST, PROD_N_TestConnectByPath)
{
	TEEHANDLE handle = TEEHANDLE_ZERO;
	struct MeTeeTESTParams intf = GetParam();
	TEESTATUS status;
	wchar_t devicePath[MAX_PATH] = {0};

	status = GetDevicePath((intf.device) ? intf.device : &GUID_DEVINTERFACE_HECI, devicePath, MAX_PATH);
	if (status)
		GTEST_SKIP();
	ASSERT_EQ(TEE_SUCCESS, TeeInit(&handle, intf.client, devicePath));
}

TEST_P(MeTeeNTEST, PROD_N_TestConnectByWrongPath)
{
	TEEHANDLE handle = TEEHANDLE_ZERO;

	ASSERT_EQ(TEE_DEVICE_NOT_FOUND, TeeInit(&handle, &GUID_NON_EXISTS_CLIENT, L"\\NO_SUCH_DEVICE"));
}

TEST_P(MeTeeNTEST, PROD_N_TestConnectByLongPath)
{
	TEEHANDLE handle = TEEHANDLE_ZERO;
	const wchar_t *longPath = L"\\Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";

	ASSERT_EQ(TEE_DEVICE_NOT_FOUND, TeeInit(&handle, &GUID_NON_EXISTS_CLIENT, longPath));
}
#endif // WIN32

TEST_P(MeTeeDataNTEST, PROD_N_TestFWUNullBufferWrite)
{
	size_t numOfBytes = 0;
	ASSERT_EQ(TEE_INVALID_PARAMETER, TeeWrite(&_handle, NULL, 1024, &numOfBytes, 0));
}

TEST_P(MeTeeDataNTEST, PROD_N_TestFWUZeroBufferSizeWrite)
{
	size_t numOfBytes = 0;
	std::vector<unsigned char> buf(1024);

	ASSERT_EQ(TEE_INVALID_PARAMETER, TeeWrite(&_handle, &buf[0], 0, &numOfBytes, 0));
}

TEST_P(MeTeeDataNTEST, PROD_N_TestFWUBiggerThenMtuWrite)
{
	size_t numOfBytes = 0;

	std::vector<unsigned char> buf(_handle.maxMsgLen + 10);

	ASSERT_EQ(TEE_INTERNAL_ERROR, TeeWrite(&_handle, &buf[0], buf.size(), &numOfBytes, 0));
}

#ifdef WIN32
TEST_P(MeTeeDataNTEST, PROD_N_TestSmallBufferRead)
{
	size_t WriteNumberOfBytes = 0;
	size_t NumberOfBytes = 0;
	std::vector<char> MaxResponse;
	const size_t Len = 1;

	MaxResponse.resize(Len);

	ASSERT_EQ(SUCCESS, TeeWrite(&_handle, &MkhiRequest, sizeof(GEN_GET_FW_VERSION), &WriteNumberOfBytes, 0));
	ASSERT_EQ(sizeof(GEN_GET_FW_VERSION), WriteNumberOfBytes);

	ASSERT_EQ(TEE_INSUFFICIENT_BUFFER, TeeRead(&_handle, &MaxResponse[0], Len, &NumberOfBytes, 0));
}
#endif // WIN32

struct MeTeeTESTParams interfaces[1] = {
	{"PCH", NULL, &GUID_DEVINTERFACE_MKHI}};

INSTANTIATE_TEST_SUITE_P(MeTeeTESTInstance, MeTeeTEST,
		testing::ValuesIn(interfaces),
		[](const testing::TestParamInfo<MeTeeTEST::ParamType>& info) {
			return info.param.name;
		});

INSTANTIATE_TEST_SUITE_P(MeTeeNTESTInstance, MeTeeNTEST,
		testing::ValuesIn(interfaces),
		[](const testing::TestParamInfo<MeTeeNTEST::ParamType>& info) {
			return info.param.name;
		});

INSTANTIATE_TEST_SUITE_P(MeTeeDataNTESTInstance, MeTeeDataNTEST,
		testing::ValuesIn(interfaces),
		[](const testing::TestParamInfo<MeTeeDataNTEST::ParamType>& info) {
			return info.param.name;
		});

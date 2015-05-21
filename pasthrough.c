

#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>


#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

#define SETTINGS_AUTORUNINF 0x01
#define SETTINGS_EXECUTABLES SETTINGS_AUTORUNINF << 1
#define SETTINGS_BLOCK SETTINGS_AUTORUNINF << 2
#define SETTINGS_READONLY SETTINGS_AUTORUNINF << 3
#define SETTINGS_NOFILEEXECUTE SETTINGS_AUTORUNINF << 4





PFLT_FILTER Filter;



#define pasthrough_INSTANCE_CONTEXT_TAG 'cIxC'

typedef struct _pasthrough_INSTANCE_CONTEXT {
	BOOLEAN ucFlags;
} pasthrough_INSTANCE_CONTEXT, *Ppasthrough_INSTANCE_CONTEXT;

#define pasthrough_INSTANCE_CONTEXT_SIZE sizeof(pasthrough_INSTANCE_CONTEXT)

NTSTATUS DriverEntry(__in PDRIVER_OBJECT DriverObject, __in PUNICODE_STRING RegistryPath);
NTSTATUS pasthroughUnload(__in FLT_FILTER_UNLOAD_FLAGS Flags);
NTSTATUS pasthroughQueryTeardown(__in PCFLT_RELATED_OBJECTS FltObjects, __in FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags);
FLT_PREOP_CALLBACK_STATUS pasthroughPreCreate(__inout PFLT_CALLBACK_DATA Data, __in PCFLT_RELATED_OBJECTS FltObjects, __deref_out_opt PVOID *CompletionContext);
FLT_PREOP_CALLBACK_STATUS pasthroughPreSetInformation(__inout PFLT_CALLBACK_DATA Data, __in PCFLT_RELATED_OBJECTS FltObjects, __deref_out_opt PVOID *CompletionContext);
NTSTATUS pasthroughInstanceSetup(__in PCFLT_RELATED_OBJECTS FltObjects, __in FLT_INSTANCE_SETUP_FLAGS Flags, __in DEVICE_TYPE VolumeDeviceType, __in FLT_FILESYSTEM_TYPE VolumeFilesystemType);
VOID ReadRegistrySettings(__in PUNICODE_STRING RegistryPath);
VOID pasthroughContextCleanup(__in PFLT_CONTEXT Context, __in FLT_CONTEXT_TYPE ContextType);
VOID pasthroughInstanceTeardownComplete(__in PCFLT_RELATED_OBJECTS FltObjects, __in FLT_INSTANCE_TEARDOWN_FLAGS Flags);





const UNICODE_STRING usAutorunInf = RTL_CONSTANT_STRING(L"autorun.inf");
const UNICODE_STRING usParentDir = RTL_CONSTANT_STRING(L"\\");



//  Assign text sections for each routine.

#ifdef ALLOC_PRAGMA
	#pragma alloc_text(INIT, DriverEntry)
	#pragma alloc_text(PAGE, pasthroughUnload)
	#pragma alloc_text(PAGE, pasthroughInstanceSetup)
	#pragma alloc_text(PAGE, pasthroughPreCreate)
	#pragma alloc_text(PAGE, pasthroughPreSetInformation)
	#pragma alloc_text(PAGE, pasthroughContextCleanup)
	#pragma alloc_text(PAGE, pasthroughInstanceTeardownComplete)
#endif

NTSTATUS DriverEntry(__in PDRIVER_OBJECT pDO, __in PUNICODE_STRING pusRegistryPath)
{
	NTSTATUS iNTStatus;

	const FLT_OPERATION_REGISTRATION Callbacks[] = {
		{IRP_MJ_CREATE, 0, pasthroughPreCreate, NULL},
		{IRP_MJ_SET_INFORMATION, 0, pasthroughPreSetInformation, NULL},
		{IRP_MJ_OPERATION_END}
	};

	const FLT_CONTEXT_REGISTRATION contextRegistration[] = {
		{FLT_INSTANCE_CONTEXT, 0, pasthroughContextCleanup, pasthrough_INSTANCE_CONTEXT_SIZE, pasthrough_INSTANCE_CONTEXT_TAG},
		{FLT_CONTEXT_END}
	};

	const FLT_REGISTRATION FilterRegistration = {
		sizeof(FLT_REGISTRATION),         //  Size
		FLT_REGISTRATION_VERSION,           //  Version
		0,                                  //  Flags
		contextRegistration,                //  Context Registration.
		Callbacks,                          //  Operation callbacks
		pasthroughUnload,                      //  FilterUnload
		pasthroughInstanceSetup,               //  InstanceSetup
		NULL,                             //  InstanceQueryTeardown
		NULL,                             //  InstanceTeardownStart
		pasthroughInstanceTeardownComplete,      //  InstanceTeardownComplete
		NULL,                               //  GenerateFileName
		NULL,                               //  GenerateDestinationFileName
		NULL                                //  NormalizeNameComponent
	};



	iNTStatus = FltRegisterFilter(pDO, &FilterRegistration, &Filter);

	if (!NT_SUCCESS(iNTStatus))
		return iNTStatus;

	iNTStatus = FltStartFiltering(Filter);

	if (NT_SUCCESS(iNTStatus))
		return STATUS_SUCCESS;

	FltUnregisterFilter(Filter);

	return iNTStatus;
}

NTSTATUS pasthroughUnload(__in FLT_FILTER_UNLOAD_FLAGS iFFUF)
{
	UNREFERENCED_PARAMETER(iFFUF);
        
        PAGED_CODE();


	FltUnregisterFilter(Filter);

	return STATUS_SUCCESS;
}

int VolumeToDosName(__in PFLT_VOLUME pFV)
{
	PDEVICE_OBJECT pDO = NULL;
	UNICODE_STRING usDosName;
	NTSTATUS iNTStatus;
	int iReturn = 0;

	iNTStatus = FltGetDiskDeviceObject(pFV, &pDO);
	if (NT_SUCCESS(iNTStatus))
	{
		iNTStatus = IoVolumeDeviceToDosName(pDO, &usDosName);
		if (NT_SUCCESS(iNTStatus))
		{
			iReturn = RtlUpcaseUnicodeChar(usDosName.Buffer[0]);
			ExFreePool(usDosName.Buffer);
		}
	}

	return iReturn;
}

NTSTATUS pasthroughInstanceSetup(__in PCFLT_RELATED_OBJECTS pFRO, __in FLT_INSTANCE_SETUP_FLAGS iFISF, __in DEVICE_TYPE iDT, __in FLT_FILESYSTEM_TYPE iFFT)
{
	UCHAR ucBuffer[sizeof(FLT_VOLUME_PROPERTIES)+512];
	PFLT_VOLUME_PROPERTIES pFVP = (PFLT_VOLUME_PROPERTIES)ucBuffer;
	ULONG ulLengthReceived;
	NTSTATUS iNTStatus;
	NTSTATUS iNTStatusReturn;
	Ppasthrough_INSTANCE_CONTEXT pAIC = NULL;
	int iDrive;
	int iIter;
	LARGE_INTEGER liSystemTime;
	__int64 iSystemTime;

	UNREFERENCED_PARAMETER(iFISF);
	UNREFERENCED_PARAMETER(iFFT);

	ulLengthReceived = 0;

	//ReadRegistrySettings(NULL);

	PAGED_CODE();

	iDrive = VolumeToDosName(pFRO->Volume);

	iNTStatus = FltAllocateContext(pFRO->Filter, FLT_INSTANCE_CONTEXT, pasthrough_INSTANCE_CONTEXT_SIZE, NonPagedPool, &pAIC);

	if (!NT_SUCCESS(iNTStatus))
	{
		if (pAIC != NULL)
			FltReleaseContext(pAIC);
		return STATUS_FLT_DO_NOT_ATTACH;
	}

	
	DbgPrint("pasthrough: pasthroughInstanceSetup iDT: %d", iDT);
			
	switch(iDT)
         {
                case FILE_DEVICE_DISK_FILE_SYSTEM: 
			
			if (NT_SUCCESS(FltGetVolumeProperties(pFRO->Volume, pFVP, sizeof(ucBuffer), &ulLengthReceived)))
			{
				DbgPrint("pasthrough: pasthroughInstanceSetup FILE_DEVICE_DISK_FILE_SYSTEM: RealDeviceName: %wZ DeviceCharacteristics: %08x", &pFVP->RealDeviceName, pFVP->DeviceCharacteristics);
				if (pFVP->DeviceCharacteristics & FILE_REMOVABLE_MEDIA)
				{
					pAIC->ucFlags = TRUE;
					iNTStatusReturn = STATUS_SUCCESS;
                                       
				}
			}
			break;

                  default:
        		  iNTStatusReturn = STATUS_FLT_DO_NOT_ATTACH;
         }
	
			
	
	DbgPrint("pasthrough: pasthroughInstanceSetup iNTStatusReturn: %d", iNTStatusReturn);

	iNTStatus = FltSetInstanceContext(pFRO->Instance, FLT_SET_CONTEXT_KEEP_IF_EXISTS, pAIC, NULL);

	if (!NT_SUCCESS(iNTStatus))
	{
		if (pAIC != NULL)
			FltReleaseContext(pAIC);
		return STATUS_FLT_DO_NOT_ATTACH;
	}

	if (pAIC != NULL)
		FltReleaseContext(pAIC);

	if (NT_SUCCESS(iNTStatusReturn))
	{
		if (ulLengthReceived <= 0)
			FltGetVolumeProperties(pFRO->Volume, pFVP, sizeof(ucBuffer), &ulLengthReceived);
		DbgPrint("pasthrough: Instance setup iDT: %d RealDeviceName: %wZ DeviceCharacteristics: %08x Dosname: %c", iDT, &pFVP->RealDeviceName, pFVP->DeviceCharacteristics, iDrive);
	}

	return iNTStatusReturn;
}

BOOLEAN MatchAutorunInf(PFLT_FILE_NAME_INFORMATION pFFNI)
{
	return RtlCompareUnicodeString(&usAutorunInf, &pFFNI->FinalComponent, TRUE) == 0 && RtlCompareUnicodeString(&usParentDir, &pFFNI->ParentDir, TRUE) == 0;
}


FLT_PREOP_CALLBACK_STATUS pasthroughPreCreate(__inout PFLT_CALLBACK_DATA pFCD, __in PCFLT_RELATED_OBJECTS pFRO, __deref_out_opt PVOID *ppvCompletionContext)
{
	PFLT_FILE_NAME_INFORMATION pFFNI;
	NTSTATUS iNTStatus;
	BOOLEAN bMatchFound;
	Ppasthrough_INSTANCE_CONTEXT pAIC = NULL;
	BOOLEAN ucFlags;

	UNREFERENCED_PARAMETER(pFRO);
	UNREFERENCED_PARAMETER(ppvCompletionContext);

	PAGED_CODE();

	iNTStatus = FltGetInstanceContext(pFCD->Iopb->TargetInstance, &pAIC);
	if (!NT_SUCCESS(iNTStatus))
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	ucFlags = pAIC->ucFlags;
	if (pAIC != NULL)
		FltReleaseContext(pAIC);

	iNTStatus = FltGetFileNameInformation(pFCD, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &pFFNI);
	if (!NT_SUCCESS(iNTStatus))
		return FLT_PREOP_SUCCESS_NO_CALLBACK;

	FltParseFileNameInformation(pFFNI);
	bMatchFound = FALSE;
	/*if (!bMatchFound && ucFlags & SETTINGS_AUTORUNINF)
		bMatchFound = MatchAutorunInf(pFFNI);*/
	
	if (!bMatchFound && ucFlags & SETTINGS_BLOCK)
		bMatchFound = TRUE;
  
	//if (!bMatchFound && ucFlags & SETTINGS_READONLY)
        {
	       // pFCD->Iopb->Parameters.Create.SecurityContext->DesiredAccess(DELETE|FILE_WRITE_DATA|FILE_WRITE_ATTRIBUTES|FILE_WRITE_EA|FILE_APPEND_DATA|WRITE_DAC|WRITE_OWNER);  
           	bMatchFound = (pFCD->Iopb->Parameters.Create.Options >> 24 ) != 0;
                DbgPrint("%wZ",pFCD->Iopb->Parameters.Create.Options);
        }
	if (!bMatchFound && ucFlags & SETTINGS_NOFILEEXECUTE)
		bMatchFound = (pFCD->Iopb->Parameters.Create.SecurityContext->DesiredAccess & FILE_EXECUTE) != 0;
	FltReleaseFileNameInformation(pFFNI);

	if (!bMatchFound)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	else
	{
		DbgPrint("pasthrough pre Create: access denied");
                  
		pFCD->IoStatus.Status = STATUS_ACCESS_DENIED;
		pFCD->IoStatus.Information = 0;

		return FLT_PREOP_COMPLETE;
	}
}

FLT_PREOP_CALLBACK_STATUS pasthroughPreSetInformation(__inout PFLT_CALLBACK_DATA pFCD, __in PCFLT_RELATED_OBJECTS pFRO, __deref_out_opt PVOID *ppvCompletionContext)
{
	PFILE_RENAME_INFORMATION pFRI;
	PFLT_FILE_NAME_INFORMATION pFFNI;
	BOOLEAN bMatchFound;

	UNREFERENCED_PARAMETER(ppvCompletionContext);

	PAGED_CODE();
    
	if (FileRenameInformation == pFCD->Iopb->Parameters.SetFileInformation.FileInformationClass)
	{
		pFRI = (FILE_RENAME_INFORMATION *)pFCD->Iopb->Parameters.SetFileInformation.InfoBuffer;
		FltGetDestinationFileNameInformation(pFRO->Instance, pFRO->FileObject, pFRI->RootDirectory, pFRI->FileName, pFRI->FileNameLength, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &pFFNI);
		FltParseFileNameInformation(pFFNI);
		bMatchFound = MatchAutorunInf(pFFNI);
                
		FltReleaseFileNameInformation(pFFNI);
               
		if (bMatchFound)
		{
			DbgPrint("pasthrough pre SetInformation: rename to \\autorun.inf access denied");


			pFCD->IoStatus.Status = STATUS_ACCESS_DENIED;
			pFCD->IoStatus.Information = 0;

			return FLT_PREOP_COMPLETE;
		}
	}

	return FLT_PREOP_SUCCESS_NO_CALLBACK;
}


VOID pasthroughContextCleanup(__in PFLT_CONTEXT pFC, __in FLT_CONTEXT_TYPE pFCT)
{
	Ppasthrough_INSTANCE_CONTEXT pAIC;

	PAGED_CODE();

	if (FLT_INSTANCE_CONTEXT == pFCT)
	{
		pAIC = (Ppasthrough_INSTANCE_CONTEXT) pFC;
		// no memory to release
	}
}

VOID pasthroughInstanceTeardownComplete(__in PCFLT_RELATED_OBJECTS pFRO, __in FLT_INSTANCE_TEARDOWN_FLAGS iFITF)
{
	Ppasthrough_INSTANCE_CONTEXT pAIC;
	NTSTATUS iNTStatus;

	UNREFERENCED_PARAMETER(iFITF);

	PAGED_CODE();

	iNTStatus = FltGetInstanceContext(pFRO->Instance, &pAIC);
	if (NT_SUCCESS(iNTStatus))
		FltReleaseContext(pAIC);
}

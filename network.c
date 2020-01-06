#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>

typedef unsigned int u32;
typedef unsigned long u64;
typedef struct dirent dirent;

#define U64LEN 20
#define MAXINTERFACES 10

const char* GCompressedLetter = "BKMGP";
const char* GInterfacePath = "/sys/class/net/";

typedef struct
{
	u32 Bytes;
	u32 Order;
} compressed_bytes;

typedef enum
{
	TRANSMIT,
	RECIVE
} ByteType;

static u32
StringLen(const char *String)
{
	u32 Result = 0;
	
	while(String[Result] != '\0' &&
			String[Result] != '\n')
	{
		++Result;
	}

	return Result;
}

static u64
StringToU64(const char *String)
{
	u64 Result = 0;
	u64 TenPower = 1;
	u32 StringLenght = StringLen(String);

	for(u32 i = StringLenght; i > 0; i--)
	{
		u64 Num = String[i - 1] - '0';
		Result += TenPower * Num;
		TenPower = TenPower * 10;
	}

	return Result;
}

static void
GetInterfaces(dirent *Interfaces)
{
	int DirDesc = 0;
	DirDesc = open(GInterfacePath, O_RDONLY);
	if(DirDesc == -1)
	{
		printf("Failed to open diretory with interfaces\n");
		return;
	}

	DIR *Dir;
	Dir = fdopendir(DirDesc);

	if(!Dir)
	{
		close(DirDesc);
		printf("Cannot open directory -> %s\n", GInterfacePath);
		return;
	}

	dirent *ent;
	for(u32 i = 0; i < 2; i++)
	{
		ent = readdir(Dir);
	} // Truncating "." and ".." directories

	for(u32 i = 0; i < MAXINTERFACES; i++)
	{
		ent = readdir(Dir);
		if(ent == 0)
		{
			return;
		}

		memcpy(Interfaces++, ent, sizeof(dirent));
	}

	close(DirDesc);
	closedir(Dir);
}

static void
GetWorkingInterfaces(dirent *Interfaces)
{
	GetInterfaces(Interfaces);

	for(u32 i = 0; i < MAXINTERFACES; i++)
	{
		if(Interfaces->d_ino == 0)
		{
			continue;
		}

		char FilePath[64] = { 0 };
		strcpy(FilePath, GInterfacePath);
		strcat(FilePath, Interfaces->d_name);
		strcat(FilePath, "/operstate");
		int OperStateFD = open(FilePath, O_RDONLY);
		
		char Buffer[16] = { 0 };
		read(OperStateFD, Buffer, 16);

		close(OperStateFD);

		if(strncmp(Buffer, "up", 2) != 0)
		{
			memmove(Interfaces, Interfaces + 1,
					  (MAXINTERFACES - i - 1) * sizeof(dirent));
			continue;
		} // if Buffer is not equal exactly to "up"

		++Interfaces;
	}
}

static u64
GetBytes(ByteType Type)
{
	dirent Interfaces[MAXINTERFACES] = { 0 };
	GetWorkingInterfaces(Interfaces);
	dirent *Interface = Interfaces;

	u64 Result = 0;
	for(u32 i = 0; i < MAXINTERFACES; i++)
	{
		int File = 0;
		char NumberBuffer[U64LEN + 1] = { 0 };

		if(Interface->d_ino == 0)
		{
			break;
		} // if the file id is 0, quit

		char FilePath[64] = { 0 };
		if(Type == TRANSMIT)
		{
			strcpy(FilePath, GInterfacePath);
			strcat(FilePath, Interface->d_name);
			strcat(FilePath, "/statistics/tx_bytes");

			File = open(FilePath, O_RDONLY, 0777);
		}
		else if(Type == RECIVE)
		{
			strcpy(FilePath, GInterfacePath);
			strcat(FilePath, Interface->d_name);
			strcat(FilePath, "/statistics/rx_bytes");

			File = open(FilePath, O_RDONLY, 0777);
		}

		read(File, NumberBuffer, U64LEN + 1);
		close(File);

		NumberBuffer[StringLen(NumberBuffer)] = '\0';
		Result += StringToU64(NumberBuffer);

		++Interface;
	}

	return Result;
}

static void
SaveOldBytes(ByteType Type, u64 ByteValue)
{
	int File = 0;
	int FileOFlag = 0;
	char Buffer[U64LEN + 1] = { 0 };
	sprintf(Buffer, "%lu\n", ByteValue);

	FileOFlag = O_WRONLY | O_TRUNC | O_CREAT;

	if(Type == TRANSMIT)
	{
		const char *FilePath = "/tmp/old_tx";
		File = open(FilePath, FileOFlag, 0777);
	}
	else if(Type == RECIVE)
	{
		const char *FilePath = "/tmp/old_rx";
		File = open(FilePath, FileOFlag, 0777);
	}

	write(File, Buffer, StringLen(Buffer));
	close(File);
}

static u64
ReadOldBytes(ByteType Type)
{
	u64 Result = 0;
	int File = 0;
	char Buffer[U64LEN + 1] = { 0 };

	if(Type == TRANSMIT)
	{
		const char *FilePath = "/tmp/old_tx";
		File = open(FilePath, O_RDONLY, 0777);
	}
	else if(Type == RECIVE)
	{
		const char *FilePath = "/tmp/old_rx";
		File = open(FilePath, O_RDONLY, 0777);
	}

	if(File == -1)
	{
		return GetBytes(Type);
	} // if there is no old bytes file, return old bytes as current bytes

	read(File, Buffer, U64LEN + 1);
	close(File);

	Result = StringToU64(Buffer);

	return Result;
}

static compressed_bytes
CompressBytes(u64 Bytes)
{
	compressed_bytes Result = { 0 };

	while(Bytes >= 1024)
	{
		Bytes = Bytes / 1024;
		++Result.Order;
	}

	Result.Bytes = Bytes;

	return Result;
}

static void
TrunkcateBytes(compressed_bytes *CompresedBytes)
{
	if(CompresedBytes->Order == 0)
	{
		CompresedBytes->Order = 1;
		CompresedBytes->Bytes = 0;
	}
}

static int
network_get(char *name)
{
	u64 RXBytes = GetBytes(RECIVE);
	u64 OldRX = ReadOldBytes(RECIVE);

	if(RXBytes == 0)
	{
		OldRX = 0;
	}
	else if(OldRX == 0)
	{
		OldRX = RXBytes;
	}

	u64 DiffRecv = RXBytes - OldRX;

	OldRX = RXBytes;
	SaveOldBytes(RECIVE, OldRX);

	compressed_bytes CompressedRX = CompressBytes(DiffRecv);
	TrunkcateBytes(&CompressedRX);

	u64 TXBytes = GetBytes(TRANSMIT);
	u64 OldTX = ReadOldBytes(TRANSMIT);

	if(TXBytes == 0)
	{
		OldTX = 0;
	}
	else if(OldTX == 0)
	{
		OldTX = TXBytes;
	}

	u64 DiffTran = TXBytes - OldTX;

	OldTX = TXBytes;
	SaveOldBytes(TRANSMIT, OldTX);

	compressed_bytes CompressedTX = CompressBytes(DiffTran);
	TrunkcateBytes(&CompressedTX);

	char RXPrint[16] = { 0 };
	char TXPrint[16] = { 0 };

	if(RXBytes == 0)
	{
		sprintf(RXPrint, "N/a");
	} // if RXBytes if empty, not bytes recived
	else
	{
		sprintf(RXPrint, "%u%cB/s", CompressedRX.Bytes, GCompressedLetter[CompressedRX.Order]);
	} // else print to char bytes formated

	if(TXBytes == 0)
	{
		sprintf(TXPrint, "N/a");
	} // if TXBytes if empty, not bytes recived
	else
	{
		sprintf(TXPrint, "%u%cB/s", CompressedTX.Bytes, GCompressedLetter[CompressedTX.Order]);
	} // else print to char bytes formated

	char network_str[32] = { 0 };
	sprintf(network_str, "%s %s", RXPrint, TXPrint);
	strcat(name, network_str);

	return strlen(network_str);
}

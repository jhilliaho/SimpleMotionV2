#include "busdevice.h"

#include "drivers/serial/pcserialport.h"
#include "drivers/tcpip/tcpclient.h"
#include "drivers/ftdi_d2xx/sm_d2xx.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#define BD_NONE 0
#define BD_RS 1
#define BD_FTDI 2
#define BD_TCP 3

//how much bytes available in transmit buffer
#define TANSMIT_BUFFER_LENGTH 128

unsigned long SMBusBaudrate=SM_BAUDRATE; //the next opened port (with smOpenBus) will be opened with the PBS defined here (default 460800 BPS)

typedef struct _SMBusDevice
{
	//common
    smint8 bdType;//bus device type (such as rs232 or ftdi lib or mcu UART etc). 1=rs232 lib
	smbool opened;

    SM_STATUS cumulativeSmStatus;

    //pointer used by bus device drivers
    smBusdevicePointer busDevicePointer;

    smuint8 txBuffer[TANSMIT_BUFFER_LENGTH];
    smint32 txBufferUsed;//how many bytes in buffer currently

	//used for FTDI lib only
} SMBusDevice;

//init on first open
smbool bdInitialized=smfalse;
SMBusDevice BusDevice[SM_MAX_BUSES];

//init device struct table
void smBDinit()
{
	int i;
	for(i=0;i<SM_MAX_BUSES;i++)
	{
		BusDevice[i].bdType=BD_NONE;
		BusDevice[i].opened=smfalse;
        BusDevice[i].txBufferUsed=0;
	}
	bdInitialized=smtrue;
}


//ie "COM1" "VSD2USB"
//return -1 if fails, otherwise handle number
smbusdevicehandle smBDOpen( const char *devicename )
{
	int handle;
    smbool success;

	//true on first call
	if(bdInitialized==smfalse)
		smBDinit();

	//find free handle
	for(handle=0;handle<SM_MAX_BUSES;handle++)
	{
		if(BusDevice[handle].opened==smfalse) break;//choose this
	}

	//all handles in use
	if(handle>=SM_MAX_BUSES) return -1;

        if(strncmp(devicename,"COM",3) == 0 || strncmp(devicename,"/dev/tty",8) == 0 || strncmp(devicename,"/dev/cu.",8) == 0) //use rs232 lib
	{
            BusDevice[handle].busDevicePointer=serialPortOpen( devicename, SMBusBaudrate, &success );
                if( success==smfalse )
		{
			return -1; //failed to open
		}
		BusDevice[handle].bdType=BD_RS;
        BusDevice[handle].txBufferUsed=0;
	}
    else if (validateIpAddress(devicename, NULL, NULL) == 0)
    {
        BusDevice[handle].busDevicePointer=OpenTCPPort( devicename, SMBusBaudrate, &success );
        if( success==smfalse )
        {
            return -1; //failed to open
        }
        BusDevice[handle].bdType=BD_TCP;
        BusDevice[handle].txBufferUsed=0;
    }
#ifdef FTDI_D2XX_SUPPORT
    //try to open FTDI bus by any name: FTDIn (n=index, 0 or greater) or device name (any string programmed in FTDI EEPROM)
    else
    {
        BusDevice[handle].busDevicePointer=d2xxPortOpen( devicename, SMBusBaudrate, &success );
            if( success==smfalse )
        {
            return -1; //failed to open
        }
        BusDevice[handle].bdType=BD_FTDI;
        BusDevice[handle].txBufferUsed=0;
    }
#else
    else//no other bus types supproted yet
    {
        smDebug( -1, Low, "smBDOpen device name argument syntax didn't match any supported driver port name");
        return -1;
    }
#endif

	//success
    BusDevice[handle].cumulativeSmStatus=0;
	BusDevice[handle].opened=smtrue;
	return handle;
}

smbool smIsBDHandleOpen( const smbusdevicehandle handle )
{
	if(handle<0) return smfalse;
	if(handle>=SM_MAX_BUSES) return smfalse;
	return BusDevice[handle].opened;
}

//return true if ok
smbool smBDClose( const smbusdevicehandle handle )
{
	//check if handle valid & open
	if( smIsBDHandleOpen(handle)==smfalse ) return smfalse;

	if( BusDevice[handle].bdType==BD_RS )
	{
        serialPortClose( BusDevice[handle].busDevicePointer );
		BusDevice[handle].opened=smfalse;
		return smtrue;
	}
    else if( BusDevice[handle].bdType==BD_TCP )
    {
        CloseTCPport( BusDevice[handle].busDevicePointer );
        BusDevice[handle].opened=smfalse;
        return smtrue;
    }
#ifdef FTDI_D2XX_SUPPORT
    else if( BusDevice[handle].bdType==BD_FTDI )
    {
        d2xxPortClose( BusDevice[handle].busDevicePointer );
        BusDevice[handle].opened=smfalse;
        return smtrue;
    }
#endif

    return smfalse;
}




//write one byte to buffer and send later with smBDTransmit()
//returns true on success
smbool smBDWrite(const smbusdevicehandle handle, const smuint8 byte )
{
	//check if handle valid & open
	if( smIsBDHandleOpen(handle)==smfalse ) return smfalse;

    if(BusDevice[handle].txBufferUsed<TANSMIT_BUFFER_LENGTH)
    {
        //append to buffer
        BusDevice[handle].txBuffer[BusDevice[handle].txBufferUsed]=byte;
        BusDevice[handle].txBufferUsed++;
        return smtrue;
    }

    return smfalse;
}

smbool smBDTransmit(const smbusdevicehandle handle)
{
    //check if handle valid & open
    if( smIsBDHandleOpen(handle)==smfalse ) return smfalse;

    if( BusDevice[handle].bdType==BD_RS )
    {
        if(serialPortWriteBuffer(BusDevice[handle].busDevicePointer,BusDevice[handle].txBuffer, BusDevice[handle].txBufferUsed)==BusDevice[handle].txBufferUsed)
        {
            BusDevice[handle].txBufferUsed=0;
            return smtrue;
        }
        else
        {
            BusDevice[handle].txBufferUsed=0;
            return smfalse;
        }
    }
    else if( BusDevice[handle].bdType==BD_TCP )
    {
        if(SendTCPBuf(BusDevice[handle].busDevicePointer,BusDevice[handle].txBuffer, BusDevice[handle].txBufferUsed)==BusDevice[handle].txBufferUsed)
        {
            BusDevice[handle].txBufferUsed=0;
            return smtrue;
        }
        else
        {
            BusDevice[handle].txBufferUsed=0;
            return smfalse;
        }
    }
#ifdef FTDI_D2XX_SUPPORT
    else if( BusDevice[handle].bdType==BD_FTDI )
    {
        if(d2xxPortWriteBuffer(BusDevice[handle].busDevicePointer,BusDevice[handle].txBuffer, BusDevice[handle].txBufferUsed)==BusDevice[handle].txBufferUsed)
        {
            BusDevice[handle].txBufferUsed=0;
            return smtrue;
        }
        else
        {
            BusDevice[handle].txBufferUsed=0;
            return smfalse;
        }
    }
#endif

    return smfalse;
}

//read one byte from bus. if byte not immediately available, block return up to SM_READ_TIMEOUT millisecs to wait data
//returns true if byte read sucessfully
smbool smBDRead( const smbusdevicehandle handle, smuint8 *byte )
{
	//check if handle valid & open
	if( smIsBDHandleOpen(handle)==smfalse ) return smfalse;

	if( BusDevice[handle].bdType==BD_RS )
	{
		int n;
        n=serialPortRead(BusDevice[handle].busDevicePointer, byte, 1);
		if( n!=1 ) return smfalse;
		else return smtrue;
	}
    else if( BusDevice[handle].bdType==BD_TCP )
    {
        int n;
        n=PollTCPPort(BusDevice[handle].busDevicePointer, byte, 1);
        if( n!=1 ) return smfalse;
        else return smtrue;
    }
#ifdef FTDI_D2XX_SUPPORT
    else if( BusDevice[handle].bdType==BD_FTDI )
    {
        int n;
        n=d2xxPortRead(BusDevice[handle].busDevicePointer, byte, 1);
        if( n!=1 ) return smfalse;
        else return smtrue;
    }
#endif

	return smfalse;
}

//BUS DEVICE INFO FETCH FUNCTIONS:

//Return number of bus devices found. details of each device may be consequently fetched by smGetBusDeviceDetails()
smint smBDGetNumberOfDetectedBuses()
{
    //only supports FTDI D2XX at the moment
#ifdef FTDI_D2XX_SUPPORT
    return d2xxGetNumberOfDetectedBuses();
#endif
    return 0;
}

smbool smBDGetBusDeviceDetails( smint index, SM_BUS_DEVICE_INFO *info )
{
    //only supports FTDI D2XX at the moment
#ifdef FTDI_D2XX_SUPPORT
    return d2xxGetBusDeviceDetails(index,info);
#endif
    return smfalse;

}

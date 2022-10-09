/* C:B**************************************************************************
This software is Copyright 2014-2016 Bright Plaza Inc. <drivetrust@drivetrust.com>

This file is part of sedutil.

sedutil is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

sedutil is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with sedutil.  If not, see <http://www.gnu.org/licenses/>.

 * C:E********************************************************************** */
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/ata/ATASMARTLib.h>
#include <IOKit/storage/nvme/NVMeSMARTLibExternal.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CFNumber.h>
#include <string.h>

#include <SEDKernelInterface/SEDKernelInterface.h>
#include "DtaDevMacOSTPer.h"

// Some macros to access the properties dicts and values
#define GetBool(dict,name) (CFBooleanRef)CFDictionaryGetValue(dict, CFSTR(name))
#define GetDict(dict,name) (CFDictionaryRef)CFDictionaryGetValue(dict, CFSTR(name))
#define GetData(dict,name) (CFDataRef)CFDictionaryGetValue(dict, CFSTR(name))
#define GetString(dict,name) (CFStringRef)CFDictionaryGetValue(dict, CFSTR(name))
#define GetNumber(dict,name) (CFNumberRef)CFDictionaryGetValue(dict, CFSTR(name))
#define GetPropertiesDict(name) GetDict(properties, name)

#define ERRORS_TO_STDERR
//#undef ERRORS_TO_STDERR


#if defined(TRY_SMART_LIBS)
static bool parseSMARTLibIdentifyData(io_service_t aBlockStorageDevice, CFDictionaryRef deviceProperties,
                                          DTA_DEVICE_INFO &disk_info)
{
    CFBooleanRef bSMARTCapable = GetBool(deviceProperties, kIOPropertySMARTCapableKey);
    if (!(bSMARTCapable != NULL))
        return false;
    if (!(CFGetTypeID(bSMARTCapable) == CFBooleanGetTypeID()))
        return false;
    if (!(CFBooleanGetValue(bSMARTCapable)))
        return false;
    
    CFDictionaryRef ioPluginTypes = GetDict( deviceProperties, kIOCFPlugInTypesKey);
    if (!(ioPluginTypes != NULL))
        return false;
    
    CFStringRef typeID= CFUUIDCreateString(CFAllocatorGetDefault(),kIOATASMARTUserClientTypeID);
    CFStringRef cfsrTypeName = (CFStringRef)CFDictionaryGetValue(ioPluginTypes, typeID);
    CFRelease(typeID);
    if (!(cfsrTypeName != NULL))
        return false;
    CFIndex typeNameLength = CFStringGetLength(cfsrTypeName);
    CFIndex typeNameMaxSize = CFStringGetMaximumSizeForEncoding(typeNameLength, kCFStringEncodingUTF8);
    char typeName[typeNameMaxSize];
            
    if (! CFStringGetCString(cfsrTypeName, typeName, typeNameMaxSize, kCFStringEncodingUTF8))
        return false;
    
    io_name_t entryName;
    IOReturn kr = IORegistryEntryGetName(aBlockStorageDevice, entryName);
    if ( !(kr == kIOReturnSuccess))
        return false;

    

    IOCFPlugInInterface     **plugInInterface = NULL;
    IONVMeSMARTInterface    **SMARTInterface = NULL;
    SInt32                  score;
    kr = IOCreatePlugInInterfaceForService(aBlockStorageDevice,
                                                    kIONVMeSMARTUserClientTypeID, kIOCFPlugInInterfaceID,
                                                    &plugInInterface, &score);
    
    if ((kIOReturnSuccess != kr) || plugInInterface == NULL) {
#if DEBUG && defined(ERRORS_TO_STDERR)
        fprintf(stderr, "IOCreatePlugInInterfaceForService returned 0x%08x -- typeName=%s entryName=%s\n", kr, typeName, entryName);   // TODO:  replace fprintf
#endif // DEBUG && defined(ERRORS_TO_STDERR)
        return false;
    }
#if DEBUG && defined(ERRORS_TO_STDERR)
    else {
        fprintf(stderr, "IOCreatePlugInInterfaceForService returned 0x%08x -- typeName=%s entryName=%s\n", kr, typeName, entryName);   // TODO:  replace fprintf
    }
#endif // DEBUG && defined(ERRORS_TO_STDERR)
    
    // Use the plugin interface to retrieve the device interface.
    HRESULT res = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIONVMeSMARTInterfaceID),
                                                     (LPVOID *)&SMARTInterface);
    // Now done with the plugin interface.
    (*plugInInterface)->Release(plugInInterface);
    
    if (0 != res || SMARTInterface == NULL) {
#if DEBUG && defined(ERRORS_TO_STDERR)
        fprintf(stderr, "QueryInterface returned %d.\n", (int) res);   // TODO:  replace fprintf
#endif // DEBUG && defined(ERRORS_TO_STDERR)
        return false;
    }
    uint32_t inNamespace = 0 ;
    IDENTIFY_RESPONSE data;
    
    kr = (*SMARTInterface) -> GetIdentifyData (SMARTInterface, &data, inNamespace );
    (*SMARTInterface)->Release(SMARTInterface);
    if ((kIOReturnSuccess != kr) ) {
#if DEBUG && defined(ERRORS_TO_STDERR)
        fprintf(stderr, "GetIdentifyData returned 0x%08x\n", kr);   // TODO:  replace fprintf
#endif // DEBUG && defined(ERRORS_TO_STDERR)
        return false;
    }
    
    memcpy(disk_info.serialNum, data.serialNumber, sizeof(disk_info.serialNum));
    memcpy(disk_info.firmwareRev, data.firmwareRevision, sizeof(disk_info.firmwareRev));
    memcpy(disk_info.modelNum, data.modelNum, sizeof(disk_info.modelNum));
    memcpy(disk_info.worldWideName, data.worldWideName, sizeof(disk_info.worldWideName));
    
    return true;
}


static bool parseNVMeSMARTLibIdentifyData(io_service_t aBlockStorageDevice, CFDictionaryRef deviceProperties,
                                          DTA_DEVICE_INFO &disk_info)
{
    CFBooleanRef bNVMeSMARTCapable = GetBool(deviceProperties, kIOPropertyNVMeSMARTCapableKey);
    if (!(bNVMeSMARTCapable != NULL))
        return false;
    if (!(CFGetTypeID(bNVMeSMARTCapable) == CFBooleanGetTypeID()))
        return false;
    if (!(CFBooleanGetValue(bNVMeSMARTCapable)))
        return false;
    
    CFDictionaryRef ioPluginTypes = GetDict( deviceProperties, kIOCFPlugInTypesKey);
    if (!(ioPluginTypes != NULL))
        return false;
    
    CFStringRef typeID= CFUUIDCreateString(CFAllocatorGetDefault(),kIONVMeSMARTUserClientTypeID);
    CFStringRef cfsrTypeName = (CFStringRef)CFDictionaryGetValue(ioPluginTypes, typeID);
    CFRelease(typeID);
    if (!(cfsrTypeName != NULL))
        return false;
    CFIndex typeNameLength = CFStringGetLength(cfsrTypeName);
    CFIndex typeNameMaxSize = CFStringGetMaximumSizeForEncoding(typeNameLength, kCFStringEncodingUTF8);
    char typeName[typeNameMaxSize];
    if (! CFStringGetCString(cfsrTypeName, typeName, typeNameMaxSize, kCFStringEncodingUTF8))
        return false;

    io_name_t entryName;
    IOReturn kr = IORegistryEntryGetName(aBlockStorageDevice, entryName);
    if ( !(kr == kIOReturnSuccess))
        return false;


    IOCFPlugInInterface     **plugInInterface = NULL;
    IONVMeSMARTInterface    **NVMeSMARTInterface = NULL;
    SInt32                  score;
    kr = IOCreatePlugInInterfaceForService(aBlockStorageDevice,
                                                    kIONVMeSMARTUserClientTypeID, kIOCFPlugInInterfaceID,
                                                    &plugInInterface, &score);
    
    if ((kIOReturnSuccess != kr) || plugInInterface == NULL) {
#if DEBUG && defined(ERRORS_TO_STDERR)
        fprintf(stderr, "IOCreatePlugInInterfaceForService returned 0x%08x -- typeName=%s entryName=%s\n", kr, typeName, entryName);   // TODO:  replace fprintf
#endif // DEBUG && defined(ERRORS_TO_STDERR)
        return false;
    }
#if DEBUG && defined(ERRORS_TO_STDERR)
    else {
        fprintf(stderr, "IOCreatePlugInInterfaceForService returned 0x%08x -- typeName=%s entryName=%s\n", kr, typeName, entryName);   // TODO:  replace fprintf
    }
#endif // DEBUG && defined(ERRORS_TO_STDERR)

    // Use the plugin interface to retrieve the device interface.
    HRESULT res = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIONVMeSMARTInterfaceID),
                                                     (LPVOID *)&NVMeSMARTInterface);
    // Now done with the plugin interface.
    (*plugInInterface)->Release(plugInInterface);
    
    if (0 != res || NVMeSMARTInterface == NULL) {
#if DEBUG && defined(ERRORS_TO_STDERR)
        fprintf(stderr, "QueryInterface returned %d.\n", (int) res);   // TODO:  replace fprintf
#endif // DEBUG && defined(ERRORS_TO_STDERR)
        return false;
    }
    uint32_t inNamespace = 0 ;
    NVMeIdentifyControllerStruct data;
    
    kr = (*NVMeSMARTInterface) -> GetIdentifyData (NVMeSMARTInterface, &data, inNamespace );
    (*NVMeSMARTInterface)->Release(NVMeSMARTInterface);
    if ((kIOReturnSuccess != kr) ) {
#if DEBUG && defined(ERRORS_TO_STDERR)
        fprintf(stderr, "GetIdentifyData returned 0x%08x\n", kr);   // TODO:  replace fprintf
#endif // DEBUG && defined(ERRORS_TO_STDERR)
        return false;
    }
    
    memcpy(disk_info.serialNum, data.SERIAL_NUMBER, sizeof(disk_info.serialNum));  //TODO: Do these need byte flipping?
    memcpy(disk_info.firmwareRev, data.FW_REVISION, sizeof(disk_info.firmwareRev));
    memcpy(disk_info.modelNum, data.MODEL_NUMBER, sizeof(disk_info.modelNum));
    memcpy(disk_info.worldWideName, data.IEEE_OUI_ID, sizeof(disk_info.worldWideName));
    
    return true;
}
#endif // defined(TRY_SMART_LIBS)

static bool FillDeviceInfoFromProperties(CFDictionaryRef deviceProperties, CFDictionaryRef mediaProperties,
                                         DTA_DEVICE_INFO &device_info) {
    if (NULL != mediaProperties) {
        CFNumberRef size = GetNumber(mediaProperties, "Size");
        if (NULL != size) {
            CFNumberType numberType = CFNumberGetType(size);
            switch (numberType) {
                case kCFNumberLongLongType:
                    {
                        long long sSize ;
                        if (CFNumberGetValue(size, numberType, &sSize)) {
                            device_info.devSize = (uint64_t)sSize;
                        }
                    }
                    break;
                case kCFNumberSInt64Type:
                    {
                        int64_t sSize ;
                        if (CFNumberGetValue(size, numberType, &sSize)) {
                            device_info.devSize = (uint64_t)sSize;
                        }
                    }
                    break;
                default:
                    ;
            }
        }
    }
    if (NULL != deviceProperties ) {
        CFDictionaryRef deviceCharacteristics = GetDict(deviceProperties, "Device Characteristics");
        if (NULL != deviceCharacteristics) {
            CFStringRef vendorName = GetString(deviceCharacteristics, "Vendor Name");
            if (vendorName != NULL )
                CFStringGetCString(vendorName, (char *)&device_info.vendorName, sizeof(device_info.vendorName)+sizeof(device_info.null3), kCFStringEncodingASCII);
            CFStringRef modelNumber = GetString(deviceCharacteristics, "Product Name");
            if (modelNumber != NULL)
                CFStringGetCString(modelNumber, (char *)&device_info.modelNum, sizeof(device_info.modelNum)+sizeof(device_info.null2), kCFStringEncodingASCII);
            CFStringRef firmwareRevision = GetString(deviceCharacteristics, "Product Revision Level");
            if (firmwareRevision != NULL)
                CFStringGetCString(firmwareRevision, (char *)&device_info.firmwareRev, sizeof(device_info.firmwareRev)+sizeof(device_info.null1), kCFStringEncodingASCII);
            CFStringRef serialNumber = GetString(deviceCharacteristics, "Serial Number");
            if (serialNumber != NULL )
                CFStringGetCString(serialNumber, (char *)&device_info.serialNum, sizeof(device_info.serialNum)+sizeof(device_info.null0), kCFStringEncodingASCII);
            
        }
            
        CFDictionaryRef protocolProperties = GetDict(deviceProperties, "Protocol Characteristics");
        if (NULL != protocolProperties) {
            CFStringRef interconnect = GetString(protocolProperties, "Physical Interconnect");
            if (NULL != interconnect) {
                CFStringGetCString(interconnect,
                                   (char *)device_info.physicalInterconnect,
                                   sizeof(device_info.physicalInterconnect)+sizeof(device_info.null4),
                                   kCFStringEncodingASCII);
            }
            CFStringRef interconnectLocation = GetString(protocolProperties, "Physical Interconnect Location");
            if (NULL != interconnectLocation) {
                CFStringGetCString(interconnectLocation,
                                   (char *)device_info.physicalInterconnectLocation,
                                   sizeof(device_info.physicalInterconnectLocation)+sizeof(device_info.null5),
                                   kCFStringEncodingASCII);
            }
        }
        
        return true;
    }
    return false;
}

const DTA_DEVICE_INFO & DtaDevMacOSBlockStorageDevice::device_info() {
    assert(NULL != pdevice_info);
    return *pdevice_info;
}

#if defined(TRY_SMART_LIBS)
void DtaDevMacOSBlockStorageDevice::parse_properties_into_device_info(io_service_t aBlockStorageDevice) {
#else // !defined(TRY_SMART_LIBS)
void DtaDevMacOSBlockStorageDevice::parse_properties_into_device_info() {
#endif // defined(TRY_SMART_LIBS)
    if (pdevice_info == NULL)
        return;
    DTA_DEVICE_INFO & device_info = *pdevice_info;
    
    CFDictionaryRef tPerProperties = GetPropertiesDict("TPer");
    if (NULL != tPerProperties) {  // Probably not, since we are probably not a DtaDevMacOSTPer
        CFDataRef diData = GetData(tPerProperties, IODtaDeviceInfoKey);
        if (NULL != diData) {
            const uint8_t * pdi = CFDataGetBytePtr(diData);
            if (NULL != pdi) {
                memcpy(&device_info, pdi, (unsigned)CFDataGetLength(diData));
                return;
            }
        }
    }
    
#if defined(TRY_SMART_LIBS)
    bool SMARTCapable = false;
    bool NVMeSMARTCapable = false;
#endif // defined(TRY_SMART_LIBS)

    CFDictionaryRef deviceProperties = GetPropertiesDict("device");
    if (NULL != deviceProperties) {
#if defined(TRY_SMART_LIBS)
        CFBooleanRef cfbrSMARTCapable = GetBool(deviceProperties, "SMART Capable");
        if (NULL != cfbrSMARTCapable) {
            SMARTCapable = CFBooleanGetValue(cfbrSMARTCapable);
        }
        CFBooleanRef cfbrNVMeSMARTCapable = GetBool(deviceProperties, "NVMe SMART Capable");
        if (NULL != cfbrNVMeSMARTCapable) {
            NVMeSMARTCapable = CFBooleanGetValue(cfbrNVMeSMARTCapable);
        }
#endif // defined(TRY_SMART_LIBS)
        CFDictionaryRef protocolProperties = GetDict(deviceProperties, "Protocol Characteristics");
        if (NULL != protocolProperties) {
            CFStringRef interconnect = GetString(protocolProperties, "Physical Interconnect");
            if (NULL != interconnect) {
                if (CFEqual(interconnect, CFSTR("USB"))) {
                    device_info.devType = DEVICE_TYPE_USB;
                } else if (CFEqual(interconnect, CFSTR("Apple Fabric"))) {
                    device_info.devType = DEVICE_TYPE_NVME;
                } else if (CFEqual(interconnect, CFSTR("PCI-Express"))) {
                    device_info.devType = DEVICE_TYPE_OTHER;  // TODO ... what?
                } else if (CFEqual(interconnect, CFSTR("SATA"))) {
                    device_info.devType = DEVICE_TYPE_ATA;
                } else
                    device_info.devType = DEVICE_TYPE_OTHER;
            }
        }
    } else {
        device_info.devType = DEVICE_TYPE_OTHER; // TODO -- generalize for other devices when they are supported by BPTperDriver
    }

    CFDictionaryRef mediaProperties = GetPropertiesDict("media");
    
    FillDeviceInfoFromProperties(deviceProperties, mediaProperties, device_info);

#if defined(TRY_SMART_LIBS)
    if (SMARTCapable &&
        parseSMARTLibIdentifyData(aBlockStorageDevice, deviceProperties, device_info) &&
        device_info.devType == DEVICE_TYPE_OTHER) {
        device_info.devType = DEVICE_TYPE_ATA;
        }
    
    if (NVMeSMARTCapable &&
        parseNVMeSMARTLibIdentifyData(aBlockStorageDevice, deviceProperties, device_info) &&
        device_info.devType == DEVICE_TYPE_OTHER) {
        device_info.devType = DEVICE_TYPE_NVME;
    }
#endif // defined(TRY_SMART_LIBS)

#define HACK_MODELNUM_WITH_VENDORNAME
#if defined(HACK_MODELNUM_WITH_VENDORNAME)
    if (device_info.devType == DEVICE_TYPE_USB )
        {
            size_t vendorNameLength = strlen((const char *)device_info.vendorName);
            size_t modelNumLength = strlen((const char *)device_info.modelNum);
            size_t newModelNumLength = vendorNameLength + modelNumLength ;
            if (0 < newModelNumLength) {
                size_t maxModelNumLength = sizeof(device_info.modelNum);
                if (maxModelNumLength < newModelNumLength) {
                    size_t excessModelNumLength = newModelNumLength - maxModelNumLength ;
                    vendorNameLength -= excessModelNumLength;
                    newModelNumLength = maxModelNumLength;
                }
                uint8_t newModelNum[sizeof(device_info.modelNum)];
                bzero(newModelNum, sizeof(newModelNum));
                if (0 < vendorNameLength) {
                    memcpy(&newModelNum[0],device_info.vendorName,vendorNameLength);
                }
                if (0 < modelNumLength) {
                    memcpy(&newModelNum[vendorNameLength],device_info.modelNum,modelNumLength);
                }
                memcpy(device_info.modelNum,newModelNum,newModelNumLength);
            }
        }
#endif //defined(HACK_MODELNUM_WITH_VENDORNAME)

    return;

}



// Sorting order
bool DtaDevMacOSBlockStorageDevice::bsdNameLessThan(DtaDevMacOSBlockStorageDevice * a, DtaDevMacOSBlockStorageDevice * b) {
    const string & aName = a->bsdName;
    const string & bName = b->bsdName;
    const auto aNameLength = aName.length();
    const auto bNameLength = bName.length();
    if (aNameLength < bNameLength ) {
        return true;
    }
    if (bNameLength < aNameLength ) {
        return false;
    }
    return aName < bName ;
}

// extern CFMutableDictionaryRef copyProperties(io_registry_entry_t service);
// #import <Foundation/Foundation.h>
static
CFMutableDictionaryRef copyProperties(io_registry_entry_t service) {
    CFMutableDictionaryRef cfproperties = NULL;
    if (KERN_SUCCESS != IORegistryEntryCreateCFProperties(service,
                                                          &cfproperties,
                                                          CFAllocatorGetDefault(),
                                                          0)) {
        return NULL;
    }
    return cfproperties;
}


std::vector<DtaDevMacOSBlockStorageDevice *> DtaDevMacOSBlockStorageDevice::enumerateBlockStorageDevices() {
    std::vector<DtaDevMacOSBlockStorageDevice *>devices;
    io_iterator_t iterator = findMatchingServices(kIOBlockStorageDeviceClass);
    io_service_t aBlockStorageDevice;
    io_service_t media;
    io_service_t tPer;
    DTA_DEVICE_INFO * pdi;
    CFDictionaryRef deviceProperties;
    CFDictionaryRef mediaProperties;
    CFDictionaryRef tPerProperties;
    CFDictionaryRef allProperties;

    const CFIndex kCStringSize = 128;
    char nameBuffer[kCStringSize];
    bzero(nameBuffer,kCStringSize);

    string entryNameStr;
    string bsdNameStr;
    
    // Iterate over nodes of class IOBlockStorageDevice or subclass thereof
    while ( (IO_OBJECT_NULL != (aBlockStorageDevice = IOIteratorNext( iterator )))) {
        
        std::vector<const void *>keys;
        std::vector<const void *>values;
        DtaDevMacOSBlockStorageDevice * device;
        CFDictionaryRef protocolCharacteristics;
        CFStringRef physicalInterconnectLocation;
        
        deviceProperties = copyProperties( aBlockStorageDevice );
        if (NULL == deviceProperties) {
            goto finishedWithDevice;
        }
        protocolCharacteristics = GetDict(deviceProperties, "Protocol Characteristics");
        if (NULL == protocolCharacteristics) {
            goto finishedWithDevice;
        }
        physicalInterconnectLocation = GetString(protocolCharacteristics, "Physical Interconnect Location");
        if (NULL == physicalInterconnectLocation ||
            CFEqual(physicalInterconnectLocation, CFSTR("File"))) {
            goto finishedWithDevice;
        }
            
                

        keys.push_back( CFSTR("device"));
        values.push_back( deviceProperties);

        
        media = findServiceForClassInChildren(aBlockStorageDevice, kIOMediaClass);
        if (IO_OBJECT_NULL == media) {
            goto finishedWithMedia;
        }
        mediaProperties = copyProperties( media );
        if (NULL == mediaProperties ) {
            goto finishedWithMedia ;
        }
        keys.push_back( CFSTR("media"));
        values.push_back( mediaProperties);


        tPer = findParent(aBlockStorageDevice);
        if (IOObjectConformsTo(tPer, kBrightPlazaDriverClass)) {
            tPerProperties = copyProperties( tPer );
            keys.push_back( CFSTR("TPer"));
            values.push_back(tPerProperties);
        }
        
        
        allProperties = CFDictionaryCreate(CFAllocatorGetDefault(), keys.data(), values.data(), (CFIndex)keys.size(), NULL, NULL);

        bzero(nameBuffer,kCStringSize);
        CFStringGetCString(GetString(mediaProperties, "BSD Name"),
                           nameBuffer, kCStringSize, kCFStringEncodingUTF8);
        bsdNameStr = string(nameBuffer);
        
        bzero(nameBuffer,kCStringSize);
        GetName(media,nameBuffer);
        entryNameStr = string(nameBuffer);
        
        pdi = static_cast <DTA_DEVICE_INFO *> (malloc(sizeof(DTA_DEVICE_INFO)));
        bzero(pdi, sizeof(DTA_DEVICE_INFO));
        
        device =
#if defined(TRY_SMART_LIBS)
        getBlockStorageDevice(aBlockStorageDevice, entryNameStr, bsdNameStr, allProperties, pdi);
#else // !defined(TRY_SMART_LIBS)
        getBlockStorageDevice(entryNameStr, bsdNameStr, allProperties, pdi);
#endif // defined(TRY_SMART_LIBS)
        devices.push_back( device );

        IOObjectRelease(tPer);

    finishedWithMedia:
        IOObjectRelease(media);

    finishedWithDevice:
        IOObjectRelease(aBlockStorageDevice);
    }
    
    sort(devices.begin(), devices.end(), DtaDevMacOSBlockStorageDevice::bsdNameLessThan);
    return devices;
}

// Factory for this class or subclass instances
#if defined(TRY_SMART_LIBS)
    DtaDevMacOSBlockStorageDevice * DtaDevMacOSBlockStorageDevice::getBlockStorageDevice(io_service_t aBlockStorageDevice,
                                                                                         std::string entryName,
                                                                                         std::string bsdName,
                                                                                         CFDictionaryRef properties,
                                                                                         DTA_DEVICE_INFO * pdi) {
        
        CFDictionaryRef tPerProperties = GetPropertiesDict("TPer");
        if (NULL != tPerProperties)
            return DtaDevMacOSTPer::getTPer(aBlockStorageDevice, entryName, bsdName, tPerProperties, properties, pdi);
        else
            return new DtaDevMacOSBlockStorageDevice(aBlockStorageDevice, entryName, bsdName, properties, pdi);
    }
#else // !defined(TRY_SMART_LIBS)
    DtaDevMacOSBlockStorageDevice * DtaDevMacOSBlockStorageDevice::getBlockStorageDevice(std::string entryName,
                                                                                         std::string bsdName,
                                                                                         CFDictionaryRef properties,
                                                                                         DTA_DEVICE_INFO * pdi) {
        
        CFDictionaryRef tPerProperties = GetPropertiesDict("TPer");
        if (NULL != tPerProperties)
            return DtaDevMacOSTPer::getTPer(entryName, bsdName, tPerProperties, properties, pdi);
        else
            return new DtaDevMacOSBlockStorageDevice(entryName, bsdName, properties, pdi);
    }
#endif // defined(TRY_SMART_LIBS)

DtaDevMacOSBlockStorageDevice * DtaDevMacOSBlockStorageDevice::getBlockStorageDevice(io_service_t aBlockStorageDevice,
                                                                                     const char *devref,
                                                                                     DTA_DEVICE_INFO *pdi) {
    DtaDevMacOSBlockStorageDevice * foundDevice = nil;
    io_service_t tPer;
    io_service_t media;
    CFDictionaryRef deviceProperties;
    CFDictionaryRef mediaProperties;
    CFDictionaryRef tPerProperties;
    CFDictionaryRef allProperties;

    const size_t kCStringSize = 128;
    char nameBuffer[kCStringSize];
    bzero(nameBuffer,kCStringSize);

    string entryName;
    string bsdName;

    
    std::vector<const void *>keys;
    std::vector<const void *>values;
    
    deviceProperties = copyProperties( aBlockStorageDevice );
    if (NULL == deviceProperties) {
        goto finishedWithDevice;
    }
    keys.push_back( CFSTR("device"));
    values.push_back( deviceProperties);
    
    
    media = findServiceForClassInChildren(aBlockStorageDevice, kIOMediaClass);
    if (IO_OBJECT_NULL == media) {
        goto finishedWithMedia;
    }
    mediaProperties = copyProperties( media );
    if (NULL == mediaProperties ) {
        goto finishedWithMedia ;
    }
    keys.push_back( CFSTR("media"));
    values.push_back( mediaProperties);
    
    
    tPer = findParent(aBlockStorageDevice);
    if (IOObjectConformsTo(tPer, kBrightPlazaDriverClass)) {
        tPerProperties = copyProperties( tPer );
        keys.push_back( CFSTR("TPer"));
        values.push_back(tPerProperties);
    }
    
    
    bzero(nameBuffer,kCStringSize);
    CFStringGetCString(GetString(mediaProperties, "BSD Name"),
                       nameBuffer, (CFIndex)kCStringSize, kCFStringEncodingUTF8);
    bsdName = string(nameBuffer);
    
    if (bsdName == string(devref)) {
        bzero(nameBuffer,kCStringSize);
        GetName(media,nameBuffer);
        entryName = string(nameBuffer);
        
        allProperties = CFDictionaryCreate(CFAllocatorGetDefault(),
                                           keys.data(), values.data(), (CFIndex)keys.size(),
                                           NULL, NULL);
        
#if defined(TRY_SMART_LIBS)
        foundDevice = DtaDevMacOSBlockStorageDevice::getBlockStorageDevice(aBlockStorageDevice,
                                                                           entryName,
                                                                           bsdName,
                                                                           allProperties,
                                                                           pdi);
#else // !defined(TRY_SMART_LIBS)
        foundDevice = DtaDevMacOSBlockStorageDevice::getBlockStorageDevice(entryName,
                                                                           bsdName,
                                                                           allProperties,
                                                                           pdi);
#endif // defined(TRY_SMART_LIBS)
        
    }
    
    IOObjectRelease(tPer);

finishedWithMedia:
    IOObjectRelease(media);
    
finishedWithDevice:
    IOObjectRelease(aBlockStorageDevice);

    return foundDevice;
}

DtaDevMacOSBlockStorageDevice * DtaDevMacOSBlockStorageDevice::getBlockStorageDevice(const char * devref, DTA_DEVICE_INFO * pdi)
{
    DtaDevMacOSBlockStorageDevice * foundDevice = NULL;
    io_iterator_t iterator = findMatchingServices(kIOBlockStorageDeviceClass);
    io_service_t aBlockStorageDevice;
  
    // Iterate over nodes of class IOBlockStorageDevice or subclass thereof
     while ( foundDevice == NULL && (IO_OBJECT_NULL != (aBlockStorageDevice = IOIteratorNext( iterator )))) {
         foundDevice = getBlockStorageDevice(aBlockStorageDevice, devref, pdi);
     }
    return foundDevice;
}



DtaDevMacOSBlockStorageDevice::~DtaDevMacOSBlockStorageDevice () {
    if (properties != NULL )
        CFRelease(properties);
}

uint8_t DtaDevMacOSBlockStorageDevice::isOpal2()
{
    if (NULL == pdevice_info)
        return (uint8_t)NULL;
    else
        return pdevice_info->OPAL20;
}
uint8_t DtaDevMacOSBlockStorageDevice::isOpal1()
{
    return pdevice_info->OPAL10;
}
uint8_t DtaDevMacOSBlockStorageDevice::isEprise()
{
    return pdevice_info->Enterprise;
}

uint8_t DtaDevMacOSBlockStorageDevice::isAnySSC()
{
    return pdevice_info->ANY_OPAL_SSC;
}
uint8_t DtaDevMacOSBlockStorageDevice::MBREnabled()
{
    return pdevice_info->Locking_MBREnabled;
}
uint8_t DtaDevMacOSBlockStorageDevice::MBRDone()
{
    return pdevice_info->Locking_MBRDone;
}
uint8_t DtaDevMacOSBlockStorageDevice::Locked()
{
    return pdevice_info->Locking_locked;
}
uint8_t DtaDevMacOSBlockStorageDevice::LockingEnabled()
{
    return pdevice_info->Locking_lockingEnabled;
}
const char * DtaDevMacOSBlockStorageDevice::getVendorName()
{
    return (const char *)&pdevice_info->vendorName;
}
const char * DtaDevMacOSBlockStorageDevice::getFirmwareRev()
{
    return (const char *)&pdevice_info->firmwareRev;
}
const char * DtaDevMacOSBlockStorageDevice::getModelNum()
{
    return (const char *)&pdevice_info->modelNum;
}
const char * DtaDevMacOSBlockStorageDevice::getSerialNum()
{
    return (const char *)&pdevice_info->serialNum;
}
const char * DtaDevMacOSBlockStorageDevice::getPhysicalInterconnect()
{
    return (const char *)&pdevice_info->physicalInterconnect;
}
const char * DtaDevMacOSBlockStorageDevice::getPhysicalInterconnectLocation()
{
    return (const char *)&pdevice_info->physicalInterconnectLocation;
}
const char * DtaDevMacOSBlockStorageDevice::getBSDName()
{
    return (const char *)bsdName.c_str();
}

DTA_DEVICE_TYPE DtaDevMacOSBlockStorageDevice::getDevType()
{
    return pdevice_info->devType;
}

const std::string DtaDevMacOSBlockStorageDevice::getDevName () {
    return ("/dev/"+bsdName).substr(0,25);
}


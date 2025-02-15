#include "pch.h"
#include "XBOFS.win/WinUsbDevice.h"
#include <qthread.h>
#include <qabstracteventdispatcher.h>
#include <qeventloop.h>


using namespace XBOFSWin;

/*
Retrives a USB String Descriptor value
*/
std::optional<std::wstring> XBOFSWin::getWinUsbStringDescriptor(const WINUSB_INTERFACE_HANDLE &winUsbHandle, UCHAR index, USHORT languageId) {
    STATIC_USB_STRING_DESCRIPTOR stringDescriptor;
    stringDescriptor.bLength = 255;
    ULONG bytesReceived;
    BOOL winUsbGetDescriptorResult = WinUsb_GetDescriptor(
        winUsbHandle, USB_STRING_DESCRIPTOR_TYPE, index, languageId, (PBYTE)&stringDescriptor, stringDescriptor.bLength, &bytesReceived
    );
    if (winUsbGetDescriptorResult == FALSE) return std::nullopt;    
    UCHAR bStringBufferContentSize = stringDescriptor.bLength - sizeof(USB_STRING_DESCRIPTOR);
    WCHAR dest[126];
    wcsncpy_s(dest, 126, stringDescriptor.bString, bStringBufferContentSize);
    return std::optional<std::wstring>{std::wstring(dest)};
}

/*
Constructs the WinUsbDevice instance and starts its event loop in a separate thread
*/
WinUsbDevice::WinUsbDevice(std::wstring devicePath, std::shared_ptr<spdlog::logger> logger, QObject* parent)
: QObject(parent), devicePath(devicePath), logger(logger)
{
    
}

void WinUsbDevice::refreshSettings() {
    logger->info("Refreshing settings");
    while (settings.group() != "") settings.endGroup();
    settings.beginGroup(QString("%1/%2/%3").arg(this->vendorId, this->productId, this->serialNumber));
    bindingEnabled = settings.value(settings::BINDING_ENABLED, false).toBool();
    bindingSelector = 0;
    debugEnabled = settings.value(settings::DEBUG_ENABLED, false).toBool();
    activeProfile = settings.value(settings::ACTIVE_PROFILE, "").toString();
    guideButtonMode = (GUIDE_BUTTON_MODE)settings.value(QString("%1/%2").arg(activeProfile, settings::GUIDE_BUTTON_MODE), 0).toInt();
    // Configure control bindings
    logger->info(bindingEnabled ? "Binding Enabled" : "Binding Disabled");
    for (int bindingsSelector = 0; bindingsSelector < 2; bindingsSelector++) {
        for (int xboArcadeStickButtonSelector = 0; xboArcadeStickButtonSelector < 15; xboArcadeStickButtonSelector++) {
            auto key = QString("%1/%2/%3").arg(activeProfile, QString::number(bindingsSelector), QString::number(xboArcadeStickButtonSelector));
            auto bindEnabled = settings.value(QString("%1/%2").arg(key, settings::BIND_ENABLED), false).toBool();
            for (int outputValueSelector = 0; outputValueSelector < 7; outputValueSelector++) {
                auto value = bindingEnabled && bindEnabled
                    ? settings.value(QString("%1/%2").arg(key, QString::number(outputValueSelector)), 0).toInt() // Binding is enabled in general and for this control
                    : defaultBindings[bindingsSelector][xboArcadeStickButtonSelector][ON][outputValueSelector]; // Binding is not enabled or is disabled for this control
                bindings[bindingsSelector][xboArcadeStickButtonSelector][OFF][outputValueSelector] = 0;
                bindings[bindingsSelector][xboArcadeStickButtonSelector][ON][outputValueSelector] = value;
            }
        }
    }
    // Configure debugging
    logger->info(debugEnabled ? "Debug Mode Enabled" : "Debug Mode Disabled");
    readInputInnerLoop = debugEnabled ? &WinUsbDevice::debugEnabledReadInputInnerLoop : &WinUsbDevice::debugDisabledReadInputInnerLoop;
}

void WinUsbDevice::run() {    
    logger->info("Entered run()");
    bool loop = true;
    int failedOpens = 0;
    int failedReads = 0;
    int failedWrites = 0;    
    // Allocate objects for VigEm        
    vigEmClient = vigem_alloc();    
    vigEmTarget = vigem_target_x360_alloc();
    // Connect to VigEm
    logger->info("Connecting VigEmClient");    
    emit vigEmConnect(devicePath);
    if (!VIGEM_SUCCESS(vigem_connect(vigEmClient))) {
        emit vigEmError(devicePath);
        logger->error("Unable to connect VigEmClient");
        loop = false;
    }   
    emit vigEmConnected(devicePath);
    // Add VigEm Target
    logger->info("Adding VigEmTarget");
    emit vigEmTargetAdd(devicePath);
    if (!VIGEM_SUCCESS(vigem_target_add(vigEmClient, vigEmTarget))) {
        emit vigEmError(devicePath);
        logger->error("Unable to add VigEmTarget");
        loop = false;
    }
    emit vigEmTargetAdded(devicePath);    
    // Get extra info from VigEmTarget
    ULONG vigEmTargetIndex;
    if (VIGEM_SUCCESS(vigem_target_x360_get_user_index(vigEmClient, vigEmTarget, &vigEmTargetIndex))) {
        emit vigEmTargetInfo(
            devicePath,
            vigem_target_get_vid(vigEmTarget),
            vigem_target_get_pid(vigEmTarget),
            vigEmTargetIndex
        );
    }
    // Loop reading input, processing it and dispatching it
    logger->info("Starting Read-Process-Dispatch loop");
    while (loop && !QThread::currentThread()->isInterruptionRequested()) {      
        QThread::currentThread()->eventDispatcher()->processEvents(QEventLoop::AllEvents);
        // Open WinUsbDevice
        emit winUsbDeviceOpen(devicePath);
        if (!openDevice()) {
            emit winUsbDeviceError(devicePath);            
            if (failedOpens == 1000) {
                logger->error("Unable to open WinUSB device");
                failedOpens = 0;
            }
            else failedOpens++;
            continue;
        }
        emit winUsbDeviceOpened(devicePath);
        // Init WinUsbDevice
        emit winUsbDeviceInit(devicePath);       
        if (!initXBOArcadeStick()) {
            emit winUsbDeviceError(devicePath);            
            logger->error("Unable to init XBO Arcade Stick"); // TODO: Provide more details on stick vendor&product
            continue;
        }
        emit winUsbDeviceInitComplete(devicePath);
        // Refresh settings
        refreshSettings();        
        // Read input
        emit winUsbDeviceReadingInput(devicePath);        
        logger->info("Reading input from XBO Arcade Stick"); // TODO: Provide more details on stick vendor&product
        int currentFailedReads = 0;
        while (loop && currentFailedReads < 5 && !QThread::currentThread()->isInterruptionRequested()) {            
            QThread::currentThread()->eventDispatcher()->processEvents(QEventLoop::AllEvents);
            switch ((this->*readInputInnerLoop)()) {
            case READ_INPUT_INNER_LOOP_RESULT::READ_FAILED:
                logger->warn("Failed to read input from XBO Arcade Stick"); // TODO: Provide more details on stick vendor&product
                currentFailedReads += 1;
                break;
            case READ_INPUT_INNER_LOOP_RESULT::WRITE_FAILED:
                logger->warn("Failed to write to VigEm Controller");
                failedWrites += 1;
                break;
            }
        }
        if (currentFailedReads >= 5) {
            emit winUsbDeviceError(devicePath);            
            logger->warn("Failed to read input from XBO Arcade Stick 5 or more times for {}");
        }
        failedReads += currentFailedReads;
        currentFailedReads = 0;        
    }
    emit winUsbDeviceTerminating(devicePath);    
    logger->info("Completed Read-Process-Dispatch loop");
    logger->info("There were {} failed reads", failedReads);
    logger->info("There were {} failed writes", failedWrites);
    logger->info("Closing WinUSB device");
    closeDevice();
    logger->info("Removing VigEmTarget");
    vigem_target_remove(vigEmClient, vigEmTarget);
    logger->info("Disconnecting VigEmClient");
    vigem_disconnect(vigEmClient);    
    vigem_target_free(vigEmTarget);    
    vigem_free(vigEmClient);
    logger->info("Completed run()");    
}

/*
Attempt to open and initialize the WinUsb device
*/
bool WinUsbDevice::openDevice() {
    HRESULT hr = S_OK;
    BOOL    bResult;
    deviceHandlesOpen = false;
    logger->info("Opening WinUSB device");
    // Attempt to open device handle
    deviceHandle = CreateFile(devicePath.c_str(),
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL);
    if (INVALID_HANDLE_VALUE == deviceHandle) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        logger->error("Failed to open device handle due to {}", hr);
        return false;
    }
    // Initialize WinUsb handle
    bResult = WinUsb_Initialize(deviceHandle, &winUsbHandle);
    if (FALSE == bResult) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(deviceHandle);
        logger->error("Failed to initiallize WinUSB handle due to {}", hr);
        return false;
    }
    deviceHandlesOpen = true;
    // Make GetDescriptor call to device in order to ensure communications with it work
    BOOL winUsbGetDescriptorResult;
    USB_DEVICE_DESCRIPTOR winUsbDeviceDescriptor;
    ULONG bytesReceived;
    winUsbGetDescriptorResult = WinUsb_GetDescriptor(
        winUsbHandle, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, (PBYTE)&winUsbDeviceDescriptor, sizeof(winUsbDeviceDescriptor), &bytesReceived
    );
    if (winUsbGetDescriptorResult == FALSE || bytesReceived != sizeof(winUsbDeviceDescriptor)) {
        logger->error("Failed to read USB descriptor");
        closeDevice();
        return false;
    }
    vendorName = QString::fromStdWString(getWinUsbStringDescriptor(winUsbHandle, winUsbDeviceDescriptor.iManufacturer, 0x0409).value_or(L"Unknown"));
    vendorId = QString::fromStdString(fmt::format("0x{:04X}", winUsbDeviceDescriptor.idVendor));
    productName = QString::fromStdWString(getWinUsbStringDescriptor(winUsbHandle, winUsbDeviceDescriptor.iProduct, 0x0409).value_or(L"Unknown"));
    productId = QString::fromStdString(fmt::format("0x{:04X}", winUsbDeviceDescriptor.idProduct));
    serialNumber = QString::fromStdWString(getWinUsbStringDescriptor(winUsbHandle, winUsbDeviceDescriptor.iSerialNumber, 0x0409)
        .value_or(L"Unknown"))
        .remove(QRegularExpression("[^0-9A-Z]+"));
    emit winUsbDeviceInfo(devicePath, vendorId, vendorName, productId, productName, serialNumber);
    logger->info("Opened WinUSB device");
    return true;
}

/*
Attempt to close the WinUsb device
*/
bool WinUsbDevice::closeDevice() {
    if (!deviceHandlesOpen) return false;
    WinUsb_Free(winUsbHandle);
    CloseHandle(deviceHandle);
    deviceHandlesOpen = false;    
    return true;    
}

/*
Process data read from XBO Arcade Stick
*/
PACKET_TYPES WinUsbDevice::processInputFromXBOArcadeStick() {
    if (dataPacket.transferred == 0) return PACKET_TYPES::UNKNOWN;
    auto guideButtonState = false;
    switch (dataPacket.data[0]) {
    case 0x01: // Dummy packet?
        return PACKET_TYPES::DUMMY;
    case 0x03: // Heartbeat packet?
        return PACKET_TYPES::HEARTBEAT;
    case 0x07: // Guide button
        guideButtonState = dataPacket.data[4] & 0x01;
        if (!bindingEnabled) {
            buttons[(int)XBO_ARCADE_STICK_BUTTONS::GUIDE] = guideButtonState;
        }
        else {
            buttons[(int)XBO_ARCADE_STICK_BUTTONS::GUIDE] = false;
            if (guideButtonMode == GUIDE_BUTTON_MODE::HOLD) {
                bindingSelector = guideButtonState;
            }
            else if (guideButtonMode == GUIDE_BUTTON_MODE::TOGGLE && guideButtonState) {
                bindingSelector = !bindingSelector;
            }
        }
        return PACKET_TYPES::GUIDE;
    case 0x20: // Inputs
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::STICK_UP]    = dataPacket.data[05] & 0x01;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::STICK_DOWN]  = dataPacket.data[05] & 0x02;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::STICK_LEFT]  = dataPacket.data[05] & 0x04;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::STICK_RIGHT] = dataPacket.data[05] & 0x08;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::X]           = dataPacket.data[22] & 0x01;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::Y]           = dataPacket.data[22] & 0x02;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::A]           = dataPacket.data[22] & 0x10;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::B]           = dataPacket.data[22] & 0x20;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::RB]          = dataPacket.data[22] & 0x04;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::RT]          = dataPacket.data[22] & 0x40;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::LB]          = dataPacket.data[22] & 0x08;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::LT]          = dataPacket.data[22] & 0x80;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::VIEW]        = dataPacket.data[04] & 0x08;
        buttons[(int)XBO_ARCADE_STICK_BUTTONS::START]       = dataPacket.data[04] & 0x04;
        return PACKET_TYPES::BUTTON_INPUT;
    }
    return PACKET_TYPES::UNKNOWN;
}

/*
Blocking data read from end-point 0x81
*/
bool WinUsbDevice::readInputFromXBOArcadeStick()
{
    if (winUsbHandle == INVALID_HANDLE_VALUE) return false;
    return WinUsb_ReadPipe(winUsbHandle, 0x81, dataPacket.data, 30, &dataPacket.transferred, NULL);
}

/*
Sent the INIT packet to the Razor Atrox
*/
bool WinUsbDevice::initXBOArcadeStick() {
    logger->info("Init XBO Arcade Stick");
    if (winUsbHandle == INVALID_HANDLE_VALUE) return false;
    ULONG cbSent = 0;
    return WinUsb_WritePipe(winUsbHandle, 0x01, (PUCHAR)XBO_ARCADE_STICK_INIT, 5, &cbSent, 0);
}

/*
Prepare data for VigEm XB360 controller
*/
XUSB_REPORT WinUsbDevice::prepareInputForVigEmController() {
    XUSB_REPORT controllerData {};
    for (int xboButtonIndex = 0; xboButtonIndex < 15; xboButtonIndex++) {        
        auto buttonState = buttons[xboButtonIndex];
        auto & [buttonValue, ltValue, rtValue, lxValue, lyValue, rxValue, ryValue] = bindings[bindingSelector][xboButtonIndex][buttonState];
        controllerData.wButtons         |= buttonValue;
        controllerData.bRightTrigger    |= rtValue;
        controllerData.bLeftTrigger     |= ltValue;
        controllerData.sThumbLX         |= lxValue;
        controllerData.sThumbLY         |= lyValue;
        controllerData.sThumbRX         |= rxValue;
        controllerData.sThumbRY         |= ryValue;
    }
    return controllerData;
}

/*
Dispatch data to the VigEm XB360 controller
*/
bool WinUsbDevice::dispatchInputToVigEmController() {
    auto controllerData = prepareInputForVigEmController();
    const auto controllerUpdateResult = vigem_target_x360_update(vigEmClient, vigEmTarget, controllerData);
    auto result = VIGEM_SUCCESS(controllerUpdateResult);    
    return result;
}

/*
Debug-mode dispatch data to the VigEm XB360 controller
*/
bool WinUsbDevice::debugDispatchInputToVigEmController() {
    auto controllerData = prepareInputForVigEmController();
    const auto controllerUpdateResult = vigem_target_x360_update(vigEmClient, vigEmTarget, controllerData);
    auto result = VIGEM_SUCCESS(controllerUpdateResult);    
    if (result) emit debugInfoXUSB_REPORT(
        controllerData.wButtons,
        controllerData.bLeftTrigger,
        controllerData.bRightTrigger,
        controllerData.sThumbLX,
        controllerData.sThumbLY,
        controllerData.sThumbRX,
        controllerData.sThumbRY
    );
    return result;
}

WinUsbDevice::READ_INPUT_INNER_LOOP_RESULT WinUsbDevice::debugDisabledReadInputInnerLoop() {
    auto readInputResult = readInputFromXBOArcadeStick();
    if (!readInputResult) return WinUsbDevice::READ_INPUT_INNER_LOOP_RESULT::READ_FAILED;
    auto packetType = processInputFromXBOArcadeStick();
    if (!dispatchInputToVigEmController()) WinUsbDevice::READ_INPUT_INNER_LOOP_RESULT::WRITE_FAILED;
    return WinUsbDevice::READ_INPUT_INNER_LOOP_RESULT::SUCCESS;
}

WinUsbDevice::READ_INPUT_INNER_LOOP_RESULT WinUsbDevice::debugEnabledReadInputInnerLoop() {
    auto readInputResult = readInputFromXBOArcadeStick();
    auto start = std::chrono::steady_clock::now();
    if (!readInputResult) return WinUsbDevice::READ_INPUT_INNER_LOOP_RESULT::READ_FAILED;
    auto packetType = processInputFromXBOArcadeStick();
    emit debugInfoButtons(
        buttons[0], buttons[1], buttons[2], buttons[3], buttons[4], buttons[5], buttons[6], buttons[7], 
        buttons[8], buttons[9], buttons[10], buttons[11], buttons[12], buttons[13], buttons[14]
    );
    if (!debugDispatchInputToVigEmController()) WinUsbDevice::READ_INPUT_INNER_LOOP_RESULT::WRITE_FAILED;
    auto end = std::chrono::steady_clock::now();
    emit debugInfoLoopTime(std::chrono::duration_cast<std::chrono::microseconds>(end - start));
    return WinUsbDevice::READ_INPUT_INNER_LOOP_RESULT::SUCCESS;
}

